//bibliotecas basicas ======================
#include <linux/module.h>   //MODULE_LICENSE   
#include <linux/init.h>	    //module_init, module_exit
#include <linux/printk.h>   //pr_info

// Dispositivo de caracteres (lab2.1) ========
#include <linux/fs.h>      // alloc_chrdev_region, struct file_operations, struct file
#include <linux/cdev.h>    // cdev_init, cdev_add: liga o numero do dispositivo as fops
#include <linux/device.h>  // class_create, device_create: cria o arquivo em /dev

// Nome do dispositivo que o driver vai criar
#define DEVICE_NAME "pubsub"

// QUantos numeros de dispositivos vai pedir ao kernel
#define DEVCOUNT 1

// numero do dispositivo
static dev_t devno = 0;

// estrutura kernel para guardar ponteiros? "{}" zera todos campos
static struct cdev pubsub_cdev = {};

// indica que é ponteiro para class e para dev, e não estrutura em si
static struct class *cls = NULL;
static struct device *dev = NULL;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pedro Oliveira");
MODULE_DESCRIPTION("T2");

// open: um processo abriu /dev/pubsub
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
};

// Função para inicializar modulo, com modprobe
static int pubsub_init(void)
{
    pr_info("pubsub: carregado\n");
    return 0;
}


// Função para finalizar modulo, com rmmod
static void pubsub_exit(void)
{
    pr_info("pubsub: removido\n");
}


module_init(pubsub_init);
module_exit(pubsub_exit);
