# Installation Guide

## bashclient Java Package

**Package**: `org.gnu.bash.client`
**License**: GNU General Public License v3.0 or later

---

## Table of Contents

1. [System Requirements](#system-requirements)
2. [Quick Start](#quick-start)
3. [Maven Dependency](#maven-dependency)
4. [Building from Source](#building-from-source)
5. [IDE Setup](#ide-setup)
6. [Platform Notes](#platform-notes)
7. [Verification](#verification)
8. [Troubleshooting Installation](#troubleshooting-installation)

---

## System Requirements

### Java Runtime

| Requirement | Minimum | Recommended |
|-------------|---------|-------------|
| Java SE     | 11      | 17 or 21    |
| Maven       | 3.6.0   | 3.9+        |
| Disk space  | 50 MB   | 100 MB      |

Any Java 11+ distribution works:

- **Eclipse Temurin** (Adoptium) -- recommended for open-source projects
- **Oracle JDK** 11, 17, or 21
- **Amazon Corretto** 11+
- **GraalVM** 21+ (for native image experiments)
- **IBM Semeru** 11+

Verify your Java installation:

```bash
java -version
# Expected: version "11.x.x" or higher

javac -version
# Expected: javac 11.x.x or higher

mvn --version
# Expected: Apache Maven 3.6.0 or higher
```

### Operating System

| Platform       | Status      | Notes                                    |
|----------------|-------------|------------------------------------------|
| Linux x86_64   | Supported   | Primary development platform             |
| macOS x86_64   | Supported   | Requires junixsocket native library      |
| macOS aarch64  | Supported   | Apple Silicon, junixsocket 2.6+          |
| Cygwin x86_64  | Supported   | Unix sockets via Cygwin runtime          |
| Windows native | Partial     | Named Pipes transport only               |
| FreeBSD        | Untested    | Should work with junixsocket             |

### bash-server

A running `bash-server` instance is required for the client to connect to.
See the main project documentation for building and running bash-server:

```bash
# Build bash-server (from bash source root)
./configure --enable-bash-server
make -j12
make bash-server

# Start bash-server
./bash-server/bash-server --name myserver
```

---

## Quick Start

The fastest path to a working setup:

```bash
# Clone and build from source
cd /path/to/bash/bash-server/clients/java
mvn package -DskipTests

# Or install to local Maven repository
mvn install -DskipTests

# Run a quick test against a running bash-server
mvn test
```

---

## Maven Dependency

### Using as a Dependency

Add the following to your project's `pom.xml`:

```xml
<dependencies>
    <!-- bashclient core -->
    <dependency>
        <groupId>org.gnu.bash</groupId>
        <artifactId>bashclient</artifactId>
        <version>0.1.0</version>
    </dependency>
</dependencies>
```

### Full pom.xml Example

A complete `pom.xml` for a project using bashclient:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<project xmlns="http://maven.apache.org/POM/4.0.0"
         xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:schemaLocation="http://maven.apache.org/POM/4.0.0
                             http://maven.apache.org/xsd/maven-4.0.0.xsd">
    <modelVersion>4.0.0</modelVersion>

    <groupId>com.example</groupId>
    <artifactId>my-bash-app</artifactId>
    <version>1.0.0-SNAPSHOT</version>
    <packaging>jar</packaging>

    <properties>
        <maven.compiler.source>11</maven.compiler.source>
        <maven.compiler.target>11</maven.compiler.target>
        <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>
        <junixsocket.version>2.9.1</junixsocket.version>
        <jackson.version>2.17.0</jackson.version>
    </properties>

    <dependencies>
        <!-- bashclient -->
        <dependency>
            <groupId>org.gnu.bash</groupId>
            <artifactId>bashclient</artifactId>
            <version>0.1.0</version>
        </dependency>

        <!-- junixsocket for Unix domain sockets -->
        <dependency>
            <groupId>com.kohlschutter.junixsocket</groupId>
            <artifactId>junixsocket-core</artifactId>
            <version>${junixsocket.version}</version>
            <type>pom</type>
        </dependency>
        <dependency>
            <groupId>com.kohlschutter.junixsocket</groupId>
            <artifactId>junixsocket-common</artifactId>
            <version>${junixsocket.version}</version>
        </dependency>
        <dependency>
            <groupId>com.kohlschutter.junixsocket</groupId>
            <artifactId>junixsocket-native-common</artifactId>
            <version>${junixsocket.version}</version>
        </dependency>

        <!-- Jackson for JSON processing -->
        <dependency>
            <groupId>com.fasterxml.jackson.core</groupId>
            <artifactId>jackson-core</artifactId>
            <version>${jackson.version}</version>
        </dependency>
        <dependency>
            <groupId>com.fasterxml.jackson.core</groupId>
            <artifactId>jackson-databind</artifactId>
            <version>${jackson.version}</version>
        </dependency>
        <dependency>
            <groupId>com.fasterxml.jackson.core</groupId>
            <artifactId>jackson-annotations</artifactId>
            <version>${jackson.version}</version>
        </dependency>

        <!-- Testing -->
        <dependency>
            <groupId>org.junit.jupiter</groupId>
            <artifactId>junit-jupiter</artifactId>
            <version>5.10.2</version>
            <scope>test</scope>
        </dependency>
        <dependency>
            <groupId>org.junit.jupiter</groupId>
            <artifactId>junit-jupiter-api</artifactId>
            <version>5.10.2</version>
            <scope>test</scope>
        </dependency>
        <dependency>
            <groupId>org.junit.jupiter</groupId>
            <artifactId>junit-jupiter-params</artifactId>
            <version>5.10.2</version>
            <scope>test</scope>
        </dependency>
    </dependencies>

    <build>
        <plugins>
            <plugin>
                <groupId>org.apache.maven.plugins</groupId>
                <artifactId>maven-compiler-plugin</artifactId>
                <version>3.12.1</version>
                <configuration>
                    <release>11</release>
                </configuration>
            </plugin>
            <plugin>
                <groupId>org.apache.maven.plugins</groupId>
                <artifactId>maven-surefire-plugin</artifactId>
                <version>3.2.5</version>
            </plugin>
            <plugin>
                <groupId>org.apache.maven.plugins</groupId>
                <artifactId>maven-jar-plugin</artifactId>
                <version>3.3.0</version>
            </plugin>
        </plugins>
    </build>
</project>
```

### Dependency Tree

The bashclient dependency graph:

```
org.gnu.bash:bashclient:0.1.0
+--- com.kohlschutter.junixsocket:junixsocket-common:2.9.1
|    +--- com.kohlschutter.junixsocket:junixsocket-native-common:2.9.1
|    \--- org.slf4j:slf4j-api:2.0.12
+--- com.fasterxml.jackson.core:jackson-databind:2.17.0
|    +--- com.fasterxml.jackson.core:jackson-core:2.17.0
|    \--- com.fasterxml.jackson.core:jackson-annotations:2.17.0
\--- (test) org.junit.jupiter:junit-jupiter:5.10.2
     +--- org.junit.jupiter:junit-jupiter-api:5.10.2
     +--- org.junit.jupiter:junit-jupiter-engine:5.10.2
     \--- org.junit.jupiter:junit-jupiter-params:5.10.2
```

### Gradle Alternative

If you prefer Gradle over Maven:

```groovy
// build.gradle
plugins {
    id 'java'
}

java {
    sourceCompatibility = JavaVersion.VERSION_11
    targetCompatibility = JavaVersion.VERSION_11
}

repositories {
    mavenLocal()
    mavenCentral()
}

dependencies {
    implementation 'org.gnu.bash:bashclient:0.1.0'
    implementation 'com.kohlschutter.junixsocket:junixsocket-core:2.9.1'
    implementation 'com.kohlschutter.junixsocket:junixsocket-common:2.9.1'
    implementation 'com.kohlschutter.junixsocket:junixsocket-native-common:2.9.1'
    implementation 'com.fasterxml.jackson.core:jackson-databind:2.17.0'

    testImplementation 'org.junit.jupiter:junit-jupiter:5.10.2'
}

test {
    useJUnitPlatform()
}
```

```kotlin
// build.gradle.kts
plugins {
    java
}

java {
    sourceCompatibility = JavaVersion.VERSION_11
    targetCompatibility = JavaVersion.VERSION_11
}

repositories {
    mavenLocal()
    mavenCentral()
}

dependencies {
    implementation("org.gnu.bash:bashclient:0.1.0")
    implementation("com.kohlschutter.junixsocket:junixsocket-core:2.9.1")
    implementation("com.kohlschutter.junixsocket:junixsocket-common:2.9.1")
    implementation("com.kohlschutter.junixsocket:junixsocket-native-common:2.9.1")
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.0")

    testImplementation("org.junit.jupiter:junit-jupiter:5.10.2")
}

tasks.test {
    useJUnitPlatform()
}
```

---

## Building from Source

### Clone and Build

```bash
# Navigate to the Java client directory
cd /path/to/bash/bash-server/clients/java

# Build without tests (fastest)
mvn package -DskipTests

# Build with tests (requires running bash-server)
mvn package

# Install to local Maven repository
mvn install

# Generate Javadoc
mvn javadoc:javadoc

# Generate full site with reports
mvn site
```

### Build Profiles

```bash
# Development build (skip static analysis)
mvn package -Pdev

# Release build (includes source and javadoc JARs)
mvn package -Prelease

# Build with all checks enabled
mvn verify
```

### Build Output

After a successful build:

```
target/
  bashclient-0.1.0.jar              # Main artifact
  bashclient-0.1.0-sources.jar      # Source attachment (release profile)
  bashclient-0.1.0-javadoc.jar      # Javadoc attachment (release profile)
  surefire-reports/                  # Test results
  site/                              # Generated documentation
```

---

## IDE Setup

### IntelliJ IDEA

1. **Open project**: File > Open > select the `bash-server/clients/java` directory
2. IntelliJ auto-detects the `pom.xml` and imports the Maven project
3. **Set JDK**: File > Project Structure > Project > SDK = Java 11+
4. **Reload Maven**: Right-click `pom.xml` > Maven > Reload Project
5. **Run tests**: Right-click `src/test/java` > Run All Tests

**Recommended IntelliJ plugins:**

- Maven Helper (dependency analysis)
- JUnit (test runner integration)
- SonarLint (code quality)

**Run configuration for examples:**

1. Run > Edit Configurations > + > Application
2. Main class: `org.gnu.bash.client.examples.Eval`
3. Program arguments: `/tmp/bash-server-1000/sock`
4. Working directory: `$MODULE_DIR$`
5. Use classpath of module: `bashclient`

### Eclipse

1. **Import**: File > Import > Maven > Existing Maven Projects
2. Browse to the `bash-server/clients/java` directory
3. Select the `pom.xml` and click Finish
4. **Configure JRE**: Right-click project > Properties > Java Build Path > Libraries
   - Ensure JRE System Library is Java 11+
5. **Update Maven**: Right-click project > Maven > Update Project (Alt+F5)

**Eclipse preferences:**

- Java > Compiler > Compiler compliance level: 11
- Java > Installed JREs: verify JDK 11+ is listed and checked
- Maven > Installations: verify Maven 3.6+ is configured

### VS Code

1. Install the **Extension Pack for Java** (includes Maven support)
2. Open the `bash-server/clients/java` directory
3. VS Code auto-detects the Maven project
4. Use the Maven sidebar to run build targets
5. Use the Testing sidebar to run JUnit tests

**settings.json recommendations:**

```json
{
    "java.configuration.updateBuildConfiguration": "automatic",
    "java.compile.nullAnalysis.mode": "automatic",
    "java.jdt.ls.java.home": "/path/to/jdk-11",
    "maven.executable.path": "/path/to/mvn"
}
```

### NetBeans

1. File > Open Project > select the `bash-server/clients/java` directory
2. NetBeans recognizes the Maven project automatically
3. Right-click project > Build (or press F11)
4. Right-click project > Test to run all tests

---

## Platform Notes

### Linux

Linux is the primary development platform. Unix domain sockets work natively.

```bash
# Verify Unix socket support
java -cp target/bashclient-0.1.0.jar \
    org.gnu.bash.client.BashClient /tmp/bash-server-$(id -u)/sock
```

The junixsocket native library is bundled for Linux x86_64 and aarch64.
For other architectures, build junixsocket from source.

### macOS

macOS is fully supported. Both Intel and Apple Silicon are covered by
junixsocket's native library bundles.

```bash
# macOS socket paths may differ
# Default: /tmp/bash-server-$(id -u)/sock
# Or: $XDG_RUNTIME_DIR/bash-server/sock
```

Note: macOS `SO_PEERCRED` is not available. The bash-server must be started
with `--no-peercred` when accepting connections from Java clients on macOS.

### Cygwin

On Cygwin, Unix domain sockets are emulated through the Cygwin runtime.
The Java process must use a Cygwin-aware JDK or connect through junixsocket
with Cygwin's socket compatibility layer.

```bash
# Start bash-server in Cygwin
./bash-server/bash-server --name myserver --no-peercred

# Run Java client (from Cygwin bash)
java -cp target/bashclient-0.1.0.jar \
    org.gnu.bash.client.examples.Eval \
    /tmp/bash-server-$(id -u)/sock
```

**Cygwin-specific considerations:**

- Use `--no-peercred` when starting bash-server (Cygwin `SO_PEERCRED` may
  not be compatible with Java's junixsocket)
- Socket paths use Cygwin POSIX paths, not Windows paths
- Named Pipe transport (`connectNamedPipe`) is the recommended alternative
  on Cygwin/Windows for better compatibility

### Windows Native (Named Pipes)

For Windows without Cygwin, use the Named Pipe transport:

```java
try (BashClient client = BashClient.connectNamedPipe("bash-server")) {
    client.auth(token);
    EvalResult result = client.eval("echo Hello from Windows");
}
```

Windows Named Pipes do not require junixsocket. The Named Pipe transport
uses `java.io.RandomAccessFile` with the `\\.\pipe\` prefix.

---

## Verification

### Verify the Build

```bash
# Check the JAR was built correctly
jar tf target/bashclient-0.1.0.jar | head -20

# Expected output includes:
# META-INF/MANIFEST.MF
# org/gnu/bash/client/BashClient.class
# org/gnu/bash/client/NdjsonProtocol.class
# org/gnu/bash/client/Transport.class
# org/gnu/bash/client/BashClientException.class
# org/gnu/bash/client/channels/ControlChannel.class
# org/gnu/bash/client/channels/CommandChannel.class
# ...
```

### Verify Connectivity

Start a bash-server and run the verification example:

```bash
# Terminal 1: Start bash-server
./bash-server/bash-server --name verify-test --no-peercred

# Terminal 2: Run verification
TOKEN=$(cat ~/.config/bash-server/verify-test/token)
java -cp target/bashclient-0.1.0.jar \
    org.gnu.bash.client.examples.Eval \
    /tmp/bash-server-$(id -u)/sock \
    "$TOKEN"
```

### Run the Test Suite

```bash
# Run all tests
mvn test

# Run a specific test class
mvn test -Dtest=BashClientTest

# Run with verbose output
mvn test -X

# Run with specific test method
mvn test -Dtest="BashClientTest#testEval"
```

Expected test output:

```
[INFO] Tests run: XX, Failures: 0, Errors: 0, Skipped: 0
[INFO] BUILD SUCCESS
```

---

## Troubleshooting Installation

### Maven cannot resolve dependencies

```bash
# Clear local repository cache
rm -rf ~/.m2/repository/org/gnu/bash
rm -rf ~/.m2/repository/com/kohlschutter

# Force dependency download
mvn dependency:resolve -U
```

### Java version mismatch

```
[ERROR] Source option 11 is not supported. Use 7 or later.
```

Your Maven is using an older JDK. Set JAVA_HOME:

```bash
export JAVA_HOME=/path/to/jdk-11
mvn package
```

### junixsocket native library not found

```
java.lang.UnsatisfiedLinkError: Could not load native library
```

Ensure the junixsocket native library matches your platform:

```xml
<!-- Add platform-specific native library -->
<dependency>
    <groupId>com.kohlschutter.junixsocket</groupId>
    <artifactId>junixsocket-native-common</artifactId>
    <version>2.9.1</version>
</dependency>
```

For unusual architectures, see the
[junixsocket documentation](https://kohlschutter.github.io/junixsocket/).

### Tests fail with connection refused

Ensure bash-server is running before executing tests:

```bash
# Start bash-server
./bash-server/bash-server --name test --no-peercred

# Then run tests
mvn test
```

### Out of memory during build

```bash
# Increase Maven's heap size
export MAVEN_OPTS="-Xmx512m -Xms256m"
mvn package
```

---

## Next Steps

- Read the [Guide](GUIDE.md) for comprehensive usage documentation
- Review the [API Reference](API.md) for complete method documentation
- Check the [Examples](examples/README.md) for runnable code samples
- See [Architecture](ARCHITECTURE.md) for internal design details
