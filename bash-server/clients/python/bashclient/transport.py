"""Transport implementations for bash-server connections."""

import asyncio
import os
import socket
import struct
import sys
from abc import ABC, abstractmethod
from typing import Optional

from .errors import TransportError


class Transport(ABC):
    """Abstract transport for reading/writing bytes."""

    @abstractmethod
    async def read_line(self) -> bytes:
        """Read one NDJSON line (including trailing newline)."""

    @abstractmethod
    async def write(self, data: bytes) -> None:
        """Write bytes to the transport."""

    @abstractmethod
    async def close(self) -> None:
        """Close the transport."""

    @property
    @abstractmethod
    def is_open(self) -> bool:
        """Whether the transport is open."""


class UnixSocketTransport(Transport):
    """Unix domain socket transport."""

    def __init__(self) -> None:
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._open = False

    async def connect(self, path: str) -> None:
        try:
            self._reader, self._writer = await asyncio.open_unix_connection(path)
            self._open = True
        except (OSError, ConnectionError) as e:
            raise TransportError(f"cannot connect to {path}: {e}") from e

    async def read_line(self) -> bytes:
        if not self._reader:
            raise TransportError("not connected")
        try:
            line = await self._reader.readline()
            if not line:
                raise TransportError("connection closed")
            return line
        except (OSError, ConnectionError) as e:
            self._open = False
            raise TransportError(f"read error: {e}") from e

    async def write(self, data: bytes) -> None:
        if not self._writer:
            raise TransportError("not connected")
        try:
            self._writer.write(data)
            await self._writer.drain()
        except (OSError, ConnectionError) as e:
            self._open = False
            raise TransportError(f"write error: {e}") from e

    async def close(self) -> None:
        if self._writer:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except OSError:
                pass
        self._open = False

    @property
    def is_open(self) -> bool:
        return self._open


class StdioTransport(Transport):
    """Transport over stdin/stdout (for --stdio mode)."""

    def __init__(self) -> None:
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._process: Optional[asyncio.subprocess.Process] = None
        self._open = False

    async def connect_process(self, *args: str) -> None:
        """Start a subprocess and connect to its stdin/stdout."""
        try:
            self._process = await asyncio.create_subprocess_exec(
                *args,
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            self._reader = self._process.stdout
            self._writer_raw = self._process.stdin
            self._open = True
        except OSError as e:
            raise TransportError(f"cannot start process: {e}") from e

    async def connect_streams(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        """Use existing streams."""
        self._reader = reader
        self._writer_raw = None
        self._writer = writer
        self._open = True

    async def read_line(self) -> bytes:
        if not self._reader:
            raise TransportError("not connected")
        try:
            line = await self._reader.readline()
            if not line:
                raise TransportError("connection closed")
            return line
        except (OSError, ConnectionError) as e:
            self._open = False
            raise TransportError(f"read error: {e}") from e

    async def write(self, data: bytes) -> None:
        if self._writer:
            try:
                self._writer.write(data)
                await self._writer.drain()
                return
            except (OSError, ConnectionError) as e:
                self._open = False
                raise TransportError(f"write error: {e}") from e
        if self._writer_raw:
            try:
                self._writer_raw.write(data)
                await self._writer_raw.drain()
            except (OSError, ConnectionError) as e:
                self._open = False
                raise TransportError(f"write error: {e}") from e
        else:
            raise TransportError("not connected")

    async def close(self) -> None:
        if self._process:
            try:
                self._process.terminate()
                await self._process.wait()
            except OSError:
                pass
        elif self._writer:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except OSError:
                pass
        self._open = False

    @property
    def is_open(self) -> bool:
        return self._open


class FdTransport(Transport):
    """Transport over an inherited file descriptor."""

    def __init__(self) -> None:
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._open = False

    async def connect(self, fd: int) -> None:
        loop = asyncio.get_event_loop()
        try:
            reader = asyncio.StreamReader()
            protocol = asyncio.StreamReaderProtocol(reader)
            r_transport, _ = await loop.connect_read_pipe(
                lambda: protocol, os.fdopen(fd, "rb", 0)
            )
            w_fd = os.dup(fd)
            w_transport, w_protocol = await loop.connect_write_pipe(
                asyncio.streams.FlowControlMixin, os.fdopen(w_fd, "wb", 0)
            )
            writer = asyncio.StreamWriter(w_transport, w_protocol, reader, loop)
            self._reader = reader
            self._writer = writer
            self._open = True
        except OSError as e:
            raise TransportError(f"cannot open fd {fd}: {e}") from e

    async def read_line(self) -> bytes:
        if not self._reader:
            raise TransportError("not connected")
        try:
            line = await self._reader.readline()
            if not line:
                raise TransportError("connection closed")
            return line
        except (OSError, ConnectionError) as e:
            self._open = False
            raise TransportError(f"read error: {e}") from e

    async def write(self, data: bytes) -> None:
        if not self._writer:
            raise TransportError("not connected")
        try:
            self._writer.write(data)
            await self._writer.drain()
        except (OSError, ConnectionError) as e:
            self._open = False
            raise TransportError(f"write error: {e}") from e

    async def close(self) -> None:
        if self._writer:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except OSError:
                pass
        self._open = False

    @property
    def is_open(self) -> bool:
        return self._open


class NamedPipeTransport(Transport):
    """Windows Named Pipe transport (Cygwin)."""

    def __init__(self) -> None:
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._open = False

    async def connect(self, pipe_name: str) -> None:
        """Connect to a Windows Named Pipe via its Cygwin /proc path or name."""
        # On Cygwin, Named Pipes are accessible as regular files
        # The pipe name maps to /proc/sys/pipe/... or \\.\pipe\...
        # Use Unix socket-like open via the filesystem path
        try:
            if sys.platform == "win32":
                # Native Windows: use CreateFile
                raise TransportError(
                    "native Windows Named Pipe not yet supported; use Cygwin"
                )
            else:
                # Cygwin: open pipe path as a regular file/socket
                self._reader, self._writer = await asyncio.open_connection(
                    pipe_name
                )
                self._open = True
        except (OSError, ConnectionError) as e:
            raise TransportError(f"cannot connect to pipe {pipe_name}: {e}") from e

    async def read_line(self) -> bytes:
        if not self._reader:
            raise TransportError("not connected")
        try:
            line = await self._reader.readline()
            if not line:
                raise TransportError("connection closed")
            return line
        except (OSError, ConnectionError) as e:
            self._open = False
            raise TransportError(f"read error: {e}") from e

    async def write(self, data: bytes) -> None:
        if not self._writer:
            raise TransportError("not connected")
        try:
            self._writer.write(data)
            await self._writer.drain()
        except (OSError, ConnectionError) as e:
            self._open = False
            raise TransportError(f"write error: {e}") from e

    async def close(self) -> None:
        if self._writer:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except OSError:
                pass
        self._open = False

    @property
    def is_open(self) -> bool:
        return self._open
