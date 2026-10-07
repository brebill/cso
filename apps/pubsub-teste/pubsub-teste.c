#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char linha[256];                         // caixa onde a linha digitada vai ficar

    FILE *f = fopen("/dev/pubsub", "r+");    // abre o dispositivo
    if (f == NULL) {
        perror("fopen");
        return 1;
    }

    // Loop principal para sempre receber novas mensagens
    while (fgets(linha, sizeof(linha), stdin) != NULL)
    {   // repete enquanto houver linha
        // /quit e local: sai do laco e cai no fclose
        if (strcmp(linha, "/quit\n") == 0)
            break;

        // /read e local: nao vai ao modulo, le a proxima mensagem do topico do /fetch
        if (strcmp(linha, "/read\n") == 0) {
            char msg[256];
            ssize_t n = read(fileno(f), msg, sizeof(msg) - 1);   // read direto, sem buffer do stdio

            if (n < 0)
                perror("erro");
            else if (n == 0)
                printf("(fila vazia)\n");
            else {
                msg[n] = '\0';
                printf("%s\n", msg);
            }
            continue;
        }

        fwrite(linha, 1, strlen(linha), f);                // manda essa linha ao modulo

        // garante que o envio acontece agora
        if (fflush(f) != 0)
            perror("erro");   // mostra o motivo, ex.: Invalid argument
    }
    fclose(f);                               // fecha o dispositivo
    return 0;
}
