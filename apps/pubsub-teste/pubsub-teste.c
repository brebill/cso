#include <stdio.h>
#include <string.h>

int main(void)
{
    char linha[256];                         // caixa onde a linha digitada vai ficar

    FILE *f = fopen("/dev/pubsub", "r+");    // abre o dispositivo
    if (f == NULL) {
        perror("fopen");
        return 1;
    }

    // Loop principal para sempre receber novas mensagens
    while (fgets(linha, sizeof(linha), stdin) != NULL) {   // repete enquanto houver linha
        fwrite(linha, 1, strlen(linha), f);                // manda essa linha ao modulo
        fflush(f);                                         // garante que o envio acontece agora
    }

    fclose(f);                               // fecha o dispositivo
    return 0;
}