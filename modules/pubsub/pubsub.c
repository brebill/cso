//bibliotecas basicas 
#include <linux/module.h>   //MODULE_*: MODULE_LICENSE, MODULE_AUTHOR...
#include <linux/init.h>	    //module_init, module_exit: funcoes de carga e remocao
#include <linux/printk.h>   //pr_info, pr_alert: escrevem no log do kernel (dmesg)

// Dispositivo de caracteres (lab2.1)
#include <linux/fs.h>      // alloc_chrdev_region, struct file_operations, struct file
#include <linux/cdev.h>    // cdev_init, cdev_add: liga o numero do dispositivo as fops
#include <linux/device.h>  // class_create, device_create: cria o arquivo em /dev

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
    return 0;
}

// release: o processo fechou o arquivo (fclose)
static int pubsub_release(struct inode *inodep, struct file *filep)
{
    pr_info("pubsub: release\n");
    return 0;
}

// tabela: "quando acontecer X no arquivo, chame a funcao Y"
static struct file_operations fops =
{
    .open = pubsub_open,
    .release = pubsub_release,
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
