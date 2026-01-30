/* shell_stub.c - Entry point for bash.exe linking against cygbash DLL */
extern int bash_main(int argc, char **argv, char **env);

int main(int argc, char **argv, char **env)
{
    return bash_main(argc, argv, env);
}
