//bibliotecas basicas 
#include <linux/module.h>   //MODULE_*: MODULE_LICENSE, MODULE_AUTHOR...
#include <linux/init.h>	    //module_init, module_exit: funcoes de carga e remocao
#include <linux/printk.h>   //pr_info, pr_alert: escrevem no log do kernel (dmesg)

// Dispositivo de caracteres (lab2.1) ==============
#include <linux/fs.h>      // alloc_chrdev_region, struct file_operations, struct file
#include <linux/cdev.h>    // cdev_init, cdev_add: liga o numero do dispositivo as fops
#include <linux/device.h>  // class_create, device_create: cria o arquivo em /dev

// ==================================================

#include <linux/uaccess.h>  // copy_from_user

#include <linux/string.h>   // strncmp

//=====================================
// LISTA ENCADEADA DE TOPICOS
//=====================================

#include <linux/list.h>     // list_head, LIST_HEAD, list_add_tail: lista ligada do kernel
#include <linux/slab.h>     // kmalloc, kfree: alocar e liberar memoria no kernel

#include <linux/mutex.h>    // mutex_lock, mutex_unlock: so um processo por vez na lista

#include <linux/sched.h>    // current, task_pid_nr: qual processo esta rodando

// tamanho maximo do nome de um topico (com o \0)
#define TOPIC_NAME_MAX 32

// uma mensagem: o texto (alocado do tamanho exato) + o elo na fila do inscrito
struct message {
    char *text;
    struct list_head list;
};

// um inscrito: o pid do processo + a fila de mensagens + o elo na lista de inscritos do topico
struct subscriber {
    pid_t pid;
    struct list_head msgs;      // fila de mensagens deste inscrito
    struct list_head list;
};

// um topico: o nome + a lista dos seus inscritos + o elo na lista de topicos
struct topic {
    char name[TOPIC_NAME_MAX];
    struct list_head subscribers;
    struct list_head list;
};


// cabeca da lista de topicos (comeca vazia)
static LIST_HEAD(topics);

// chave que protege a lista de topicos (so quem tem a chave mexe nela)
static DEFINE_MUTEX(topics_lock);

// procura um topico pelo nome na lista, devolve NULL se nao achar
static struct topic *find_topic(const char *name)
{
    struct topic *t;

    list_for_each_entry(t, &topics, list) {
        if (strcmp(t->name, name) == 0)
            return t;
    }
    return NULL;
}

// 1 se t ainda e um topico da lista, 0 se nao (so compara enderecos, nunca le t)
static int topic_exists(const struct topic *t)
{
    struct topic *p;

    list_for_each_entry(p, &topics, list) {
        if (p == t)
            return 1;
    }
    return 0;
}

// cria um topico novo e coloca no fim da lista, devolve NULL se faltar memoria
static struct topic *create_topic(const char *name)
{
    struct topic *t = kmalloc(sizeof(*t), GFP_KERNEL);   // pede memoria ao kernel

    if (t == NULL)
        return NULL;                                     // sem memoria
    strscpy(t->name, name, TOPIC_NAME_MAX);              // copia o nome (cabe e termina com \0)

    INIT_LIST_HEAD(&t->subscribers);                     // lista de inscritos comeca vazia 

    list_add_tail(&t->list, &topics);                    // encaixa no fim da lista
    return t;
}

// procura um inscrito pelo pid na lista do topico, devolve NULL se nao achar (pid disponivel)
static struct subscriber *find_subscriber(struct topic *t, pid_t pid)
{
    struct subscriber *s;

    list_for_each_entry(s, &t->subscribers, list) {
        if (s->pid == pid)
            return s;
    }
    return NULL;
}

// cria um inscrito com o pid dado e coloca no fim da lista do topico
static struct subscriber *add_subscriber(struct topic *t, pid_t pid)
{
    struct subscriber *s = kmalloc(sizeof(*s), GFP_KERNEL);

    if (s == NULL)
        return NULL;                                     // sem memoria
    s->pid = pid;
    INIT_LIST_HEAD(&s->msgs);                            // fila comeca vazia
    list_add_tail(&s->list, &t->subscribers);            // encaixa na lista deste topico
    return s;
}

// libera todas as mensagens da fila de um inscrito
static void free_messages(struct subscriber *s)
{
    struct message *m, *tmp;

    list_for_each_entry_safe(m, tmp, &s->msgs, list) {
        list_del(&m->list);                              // tira da fila
        kfree(m->text);                                  // libera o texto
        kfree(m);                                        // libera a struct
    }
}

// copia o texto e poe a mensagem no fim da fila do inscrito, devolve 0 ou -ENOMEM
static int queue_message(struct subscriber *s, const char *text)
{
    struct message *m = kmalloc(sizeof(*m), GFP_KERNEL);

    if (m == NULL)
        return -ENOMEM;
    m->text = kstrdup(text, GFP_KERNEL);                 // copia: o kbuf do write e local
    if (m->text == NULL) {
        kfree(m);                                        // desfaz o bloco 1
        return -ENOMEM;
    }
    list_add_tail(&m->list, &s->msgs);                   // encaixa no fim da fila
    return 0;
}

// tira um inscrito da lista do topico e devolve a memoria dele (fila primeiro, depois a struct)
static void remove_subscriber(struct subscriber *s)
{
    free_messages(s);                                    // libera a fila do inscrito
    list_del(&s->list);                                  // tira da lista do topico
    kfree(s);                                            // devolve a memoria do inscrito
}

// tira o pid do topico; se o topico ficar sem inscritos, apaga o topico tambem
// devolve 0, ou -ENOENT se o pid nao estava inscrito (chamar com o mutex pego; t pode ser liberado aqui)
static int unsubscribe_pid(struct topic *t, pid_t pid)
{
    struct subscriber *s = find_subscriber(t, pid);

    if (s == NULL)
        return -ENOENT;
    remove_subscriber(s);
    if (list_empty(&t->subscribers)) {                   // ultimo inscrito saiu
        list_del(&t->list);
        kfree(t);
    }
    return 0;
}

// libera todos os topicos da lista, e os inscritos de cada um (chamado no exit)
static void free_topics(void)
{
    struct topic *t, *tmp;
    struct subscriber *s, *stmp;

    list_for_each_entry_safe(t, tmp, &topics, list) 
    {
        pr_info("pubsub: liberando topico \"%s\"\n", t->name);

        list_for_each_entry_safe(s, stmp, &t->subscribers, list)
            remove_subscriber(s);

        list_del(&t->list);                              // tira o topico da lista
        kfree(t);                                        // devolve a memoria do topico
    }
}


// debug: imprime todos os topicos da lista (chamar com o mutex pego)
static void print_topics(void)
{
    struct topic *t;
    int n = 0;

    list_for_each_entry(t, &topics, list) {
        struct subscriber *s;

        pr_info("pubsub:   topico \"%s\"\n", t->name);
        list_for_each_entry(s, &t->subscribers, list)
            pr_info("pubsub:     pid %d\n", s->pid);
        n++;
    }
    pr_info("pubsub: total %d topico(s)\n", n);
}


// ====================================================

// Nome do dispositivo que o driver vai criar
#define DEVICE_NAME "pubsub"

// Quantos numeros de dispositivo (minors) vai pedir ao kernel: so 1, o /dev/pubsub
#define DEVCOUNT 1

// numero do dispositivo (major+minor juntos)
static dev_t devno = 0;

// ficha do dispositivo de caracteres: liga o numero (devno) as funcoes (fops)
// "{}" zera todos campos, cdev_init e cdev_add preenchem depois
static struct cdev pubsub_cdev = {};

// ponteiros (o kernel aloca e devolve o endereco) para a classe e o dispositivo
// a classe e o device sao o que fazem o arquivo /dev/pubsub aparecer
static struct class *cls = NULL;
static struct device *dev = NULL;

// informacoes do modulo (modinfo)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pedro Oliveira");
MODULE_DESCRIPTION("T2");

// open: um processo abriu /dev/pubsub (fopen)
static int pubsub_open(struct inode *inodep, struct file *filep)
{
    pr_info("pubsub: open\n");
    filep->private_data = NULL;                            // nenhum topico escolhido ainda
    return 0;
}

// release: o processo fechou o arquivo (fclose)
static int pubsub_release(struct inode *inodep, struct file *filep)
{
    struct topic *t, *tmp;
    pid_t pid = task_pid_nr(current);

    pr_info("pubsub: release\n");

    // tira o pid de todos os topicos; os que ficarem vazios somem (_safe: t pode ser liberado)
    mutex_lock(&topics_lock);
    list_for_each_entry_safe(t, tmp, &topics, list)
        unsubscribe_pid(t, pid);                           // -ENOENT (nao estava la) e ignorado
    mutex_unlock(&topics_lock);
    return 0;
}

// write: o processo escreveu len bytes em buffer (fwrite/echo)
static ssize_t pubsub_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset)
{
    char kbuf[256];

    // linha grande demais
    if (len >= sizeof(kbuf))
        return -EINVAL;               
    
    // endereço do usuário inválido
    if (copy_from_user(kbuf, buffer, len))
        return -EFAULT;                    
    kbuf[len] = '\0';       // saber quando acaba a string
    if (len > 0 && kbuf[len - 1] == '\n')
    kbuf[len - 1] = '\0';    // tira o Enter do fim
    
    // /subscribe (entrar na lista X)
    if (strncmp(kbuf, "/subscribe ", 11) == 0) {
        char *topico = kbuf + 11;

        struct topic *t;
        int created = 0;                                   // 1 se este subscribe criou o topico
        pid_t pid = task_pid_nr(current);                  // quem esta fazendo o subscribe

        // nome nao cabe no topico
        if (strlen(topico) >= TOPIC_NAME_MAX)
            return -ENAMETOOLONG;                 

        // nome vazio ou com espaco
        if (topico[0] == '\0' || strchr(topico, ' '))
            return -EINVAL;                       

        // acha o topico (ou cria) e inscreve o processo; tudo com o mutex
        mutex_lock(&topics_lock);                          // pega a chave
        t = find_topic(topico);
        if (t == NULL) {
            t = create_topic(topico);
            if (t == NULL) {
                mutex_unlock(&topics_lock);                // devolve a chave antes de sair
                return -ENOMEM;                            // sem memoria
            }
            created = 1;
            pr_info("pubsub: topico \"%s\" criado\n", topico);
        }

        if (find_subscriber(t, pid) == NULL) {             // ainda nao esta inscrito
            if (add_subscriber(t, pid) == NULL) {
                if (created) {                             // nao deixa topico vazio na lista
                    list_del(&t->list);
                    kfree(t);
                }
                mutex_unlock(&topics_lock);                // devolve a chave antes de sair
                return -ENOMEM;                            // sem memoria
            }
            pr_info("pubsub: pid %d inscrito em \"%s\"\n", pid, topico);
        } else {
            pr_info("pubsub: pid %d ja inscrito em \"%s\"\n", pid, topico);
        }

        print_topics();                                    // debug: mostra a lista
        mutex_unlock(&topics_lock);                        // devolve a chave
    }

    // /unsubscribe (sair da lista x)
    else if (strncmp(kbuf, "/unsubscribe ", 13) == 0) {
        char *topico = kbuf + 13;
        struct topic *t;
        int ret;

        // nome vazio ou com espaco
        if (topico[0] == '\0' || strchr(topico, ' '))
            return -EINVAL;

        mutex_lock(&topics_lock);
        t = find_topic(topico);
        if (t == NULL) {
            mutex_unlock(&topics_lock);                    // devolve a chave antes de sair
            return -ENOENT;                                // topico nao existe
        }
        if (filep->private_data == t)                      // so compara o endereco, nao usa o topico
            filep->private_data = NULL;                    // este arquivo deixa de apontar p/ ele (t pode ser liberado)
        ret = unsubscribe_pid(t, task_pid_nr(current));    // -ENOENT se nao estava inscrito
        if (ret == 0)
            print_topics();                                // debug: mostra a lista
        mutex_unlock(&topics_lock);
        if (ret != 0)
            return ret;
        pr_info("pubsub: unsubscribe, topico=\"%s\"\n", topico);
    }


    // /fetch (escolher topico X para os proximos read
    else if (strncmp(kbuf, "/fetch ", 7) == 0) {
        char *topico = kbuf + 7;
        struct topic *t;

        // nome vazio ou com espaco
        if (topico[0] == '\0' || strchr(topico, ' '))
            return -EINVAL;

        mutex_lock(&topics_lock);
        t = find_topic(topico);
        if (t == NULL) {
            mutex_unlock(&topics_lock);                    // devolve a chave antes de sair
            return -ENOENT;                                // topico nao existe
        }
        if (find_subscriber(t, task_pid_nr(current)) == NULL) {
            mutex_unlock(&topics_lock);
            return -EPERM;                                 // nao esta inscrito nele
        }
        filep->private_data = t;                           // proximos read leem deste topico
        mutex_unlock(&topics_lock);
        pr_info("pubsub: fetch, topico=\"%s\"\n", topico);
    }

    // /publish (publicar em X, mensagem "Y")
      else if (strncmp(kbuf, "/publish ", 9) == 0) {
        char *topico = kbuf + 9;            // começa depois do comando
        char *msg = strchr(topico, ' ');    // acha o espaço que separa tópico e mensagem
        size_t n;
        struct topic *t;

        if (msg == NULL)
            return -EINVAL;                 // faltou a mensagem
        *msg = '\0';                        // termina o tópico ali
        msg++;                              // msg agora aponta para o começo da mensagem

        n = strlen(msg);
        if (n < 2 || msg[0] != '"' || msg[n - 1] != '"')
            return -EINVAL;                 // mensagem precisa estar entre aspas
        msg[n - 1] = '\0';                  // tira a aspa final
        msg++;                              // pula a aspa inicial

        // acha o topico e poe a mensagem na fila de cada inscrito; tudo com o mutex
        mutex_lock(&topics_lock);
        t = find_topic(topico);
        if (t != NULL) {                                   // topico inexistente: ignora
            struct subscriber *s;

            list_for_each_entry(s, &t->subscribers, list) {
                if (queue_message(s, msg) != 0) {
                    mutex_unlock(&topics_lock);            // devolve a chave antes de sair
                    return -ENOMEM;                        // sem memoria
                }
                pr_info("pubsub: msg \"%s\" na fila do pid %d\n", msg, s->pid);
            }
        }
        mutex_unlock(&topics_lock);
    }


    else
    return -EINVAL;         // comando descohecido

    pr_info("pubsub: write (%zu bytes)\n", len);
    return len;    // "consumi todos os bytes", se devolver 0 o app tenta de novo
}

// read: o processo quer ler (cat/fread)
static ssize_t pubsub_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    struct topic *t = filep->private_data;                 // topico escolhido no /fetch
    struct subscriber *s;
    struct message *m;
    size_t n;

    if (t == NULL)
        return -EINVAL;                                    // nao fez /fetch ainda

    mutex_lock(&topics_lock);
    if (!topic_exists(t)) {                                // o topico foi apagado depois do /fetch
        mutex_unlock(&topics_lock);
        filep->private_data = NULL;
        return -EINVAL;                                    // precisa de novo /fetch
    }
    s = find_subscriber(t, task_pid_nr(current));
    if (s == NULL || list_empty(&s->msgs)) {
        mutex_unlock(&topics_lock);
        return 0;                                          // fila vazia: nada para ler
    }

    m = list_first_entry(&s->msgs, struct message, list);  // a mais antiga
    n = strlen(m->text);
    if (n > len) {
        mutex_unlock(&topics_lock);
        return -EMSGSIZE;                                  // nao cabe no buffer do usuario; fica na fila
    }
    if (copy_to_user(buffer, m->text, n)) {
        mutex_unlock(&topics_lock);
        return -EFAULT;                                    // endereco invalido; fica na fila
    }

    list_del(&m->list);                                    // so tira da fila depois de entregar
    kfree(m->text);
    kfree(m);
    mutex_unlock(&topics_lock);

    pr_info("pubsub: read, %zu bytes entregues ao pid %d\n", n, task_pid_nr(current));
    return n;
}

// tabela: "quando acontecer X no arquivo, chame a funcao Y"
static struct file_operations fops =
{
    .open     = pubsub_open,
    .release  = pubsub_release,
    .write    = pubsub_write,
    .read     = pubsub_read,
};

// Função para inicializar modulo, com modprobe
static int pubsub_init(void)
{
    // pede ao kernel um major livre e o minor 0, o numero fica em devno
    int err = alloc_chrdev_region(&devno, 0, DEVCOUNT, DEVICE_NAME);
    // se nao conseguir, devolve o erro e o modprobe falha
    if (err != 0) {
        pr_alert("pubsub: falhou ao registrar o numero do dispositivo\n");
        return err;
    }

    // liga as funcoes (fops) a ficha do dispositivo
    cdev_init(&pubsub_cdev, &fops);

    // registra no kernel: a partir daqui, open/release do numero devno chamam as nossas funcoes
    err = cdev_add(&pubsub_cdev, devno, DEVCOUNT);
    if (err != 0) {
        pr_alert("pubsub: falhou ao adicionar o dispositivo\n");
        unregister_chrdev_region(devno, DEVCOUNT);   // desfaz o alloc de cima
        return err;
    }

    // cria a classe (/sys/class/pubsub), o device_create vai precisar dela
    // assinatura (linux/device/class.h): struct class *class_create(const char *name);
    // em erro devolve um codigo escondido no ponteiro, por isso IS_ERR e PTR_ERR
    cls = class_create(DEVICE_NAME);
    if (IS_ERR(cls)) {
        pr_alert("pubsub: falhou ao criar a classe\n");
        err = PTR_ERR(cls);                          // tira o codigo de erro de dentro do ponteiro
        cdev_del(&pubsub_cdev);                      // desfaz o cdev_add
        unregister_chrdev_region(devno, DEVCOUNT);   // desfaz o alloc
        return err;
    }

    // cria o device,  ele que faz o arquivo /dev/pubsub aparecer
    // assinatura (linux/device.h): struct device *device_create(cls, parent, devt, drvdata, fmt, ...);
    dev = device_create(cls, NULL, devno, NULL, DEVICE_NAME);
    if (IS_ERR(dev)) {
        pr_alert("pubsub: falhou ao criar o device\n");
        err = PTR_ERR(dev);                          // tira o codigo de erro de dentro do ponteiro
        class_destroy(cls);                          // desfaz a classe
        cdev_del(&pubsub_cdev);                      // desfaz o cdev_add
        unregister_chrdev_region(devno, DEVCOUNT);   // desfaz o alloc
        return err;
    }

    // Log final
    pr_info("pubsub: carregado, major=%d minor=%d\n", MAJOR(devno), MINOR(devno));
    return 0;
}


// Função para finalizar modulo, com rmmod
// desfaz tudo na ordem inversa do init
static void pubsub_exit(void)
{

    // Libera os tópicos criados
    free_topics();

    // desfaz o device (apaga /dev/pubsub), ordem inversa do init
    // assinatura (linux/device.h): void device_destroy(const struct class *cls, dev_t devt);
    device_destroy(cls, devno);


    // desfaz a classe (apaga /sys/class/pubsub)
    // assinatura (linux/device/class.h): void class_destroy(const struct class *cls);
    class_destroy(cls);

    // desfaz o cdev_add: o kernel para de chamar as nossas funcoes
    cdev_del(&pubsub_cdev);

    // devolve o numero reservado no init (tudo que o init pede, o exit devolve)
    unregister_chrdev_region(devno, DEVCOUNT);

    pr_info("pubsub: removido\n");
}


module_init(pubsub_init);
module_exit(pubsub_exit);
