#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/kfifo.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/timekeeping.h>
#include <linux/rtc.h>
#include <linux/ktime.h>

//#define THREAD_WRITE_LOG_TEST //for test
#define LOG_KFIFO_SIZE 4096 //need power of 2

#ifdef THREAD_WRITE_LOG_TEST
static struct task_struct *send_thread = NULL;
#endif

static void *fifo_buffer = NULL;
static struct kfifo log_fifo;
static struct cdev log_cdev;
static dev_t log_dev_num;
static struct class *cls;
static wait_queue_head_t read_queue;
static int open_count = 0;
static int condition = 0;
static bool log_drv_ready = false;

static int get_local_time_str(char *buf, size_t buf_size) {
    struct timespec64 ts;
    struct rtc_time tm;
    unsigned long local_time;
    int len;

    // get current time
    ktime_get_real_ts64(&ts);

    // trans to local time，sys_tz is system timezone
    local_time = ts.tv_sec - sys_tz.tz_minuteswest * 60;
    rtc_time64_to_tm(local_time, &tm);

    // format to string
    len = snprintf(buf, buf_size, "[%02d-%02d %02d:%02d:%02d.%03lu]",
            tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
    // snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d.%03lu",
    //          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
    //          tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
    return len;
}
// char time_str[32];
// get_local_time_str(time_str, sizeof(time_str));

static ssize_t dev_read(struct file *file, char __user *buf, size_t len, loff_t *off) {
    int copied = 0;
    if (kfifo_is_empty(&log_fifo)) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        wait_event_interruptible(read_queue, !kfifo_is_empty(&log_fifo));
        condition = 0;
    }

    if (kfifo_to_user(&log_fifo, buf, len, &copied))
        return -EFAULT;

    return copied;
}

static ssize_t fifo_write(const char *buf, size_t len) {
    size_t written_len,avail_len,to_write_len;

    written_len = 0;
    avail_len = kfifo_avail(&log_fifo);
    to_write_len = min(avail_len, len);
    if (to_write_len > 0) {
        written_len = kfifo_in(&log_fifo, buf, len);
        // written_len != len ?
        condition = 1;
        wake_up_interruptible(&read_queue);
        //pr_info("lkb written: %d, remain space:%d\n", written_len, kfifo_avail(&log_fifo));
    }

    if (len > to_write_len) {
        //pr_info("lkb log fifo full, want write %d, discarded %d bytes\n", len, len-to_write_len);
    }

    return written_len;
}

#if 0 //origin log
ssize_t oplus_log_write(const char *buf, size_t len) { //export API
    char time_str[32];
    int len_time;

    len_time = get_local_time_str(time_str, sizeof(time_str));
    fifo_write(time_str, len_time+1); //add time string
    fifo_write(buf, len);
    return len;
}
#else //log with time header
ssize_t oplus_log_write(const char *buf, size_t len) { //export API
    char time_str[32];
    char line_buf[256];
    int out_len = 0;
    int index = 0;

    get_local_time_str(time_str, sizeof(time_str));

    while (out_len < len && index < 254) {
        if (index == 0) {
            strcpy(line_buf, time_str);
            index += strlen(time_str);
        }
        line_buf[index] = buf[out_len];
        if (line_buf[index] == '\n') {
            //line_buf[index+1] = '\0'; //only for printf
            fifo_write(line_buf, index+1);
            index = 0;
            out_len++;
        } else {
            index++;
            out_len++;
        }
    }
    if (index != 0) {
        //line_buf[index] = '\n'; //only for printf
        //line_buf[index+1] = '\0'; //only for printf
        fifo_write(line_buf, index);
    }
    return len;
}
#endif

static int dev_open(struct inode *inode, struct file *file) {
    if (open_count)
        return -EBUSY;

    open_count++;
    try_module_get(THIS_MODULE);
    return 0;
}

static int dev_release(struct inode *inode, struct file *file) {
    open_count--;
    module_put(THIS_MODULE);
    return 0;
}

static unsigned int dev_poll(struct file *filp, struct poll_table_struct *wait)
{
    unsigned int mask = 0;

    poll_wait(filp, &read_queue, wait);
    if (condition) {
        mask |= POLLIN | POLLRDNORM;
    }
    return mask;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = dev_read,
    .open  = dev_open,
    .release = dev_release,
    .poll = dev_poll,
};

#ifdef THREAD_WRITE_LOG_TEST
static int thread_log_write_test(void *arg) {
    char buf[100];
    size_t count = 0;

    if (log_drv_ready == false)
        return 0;

    pr_info("lkb log test thread+\n");
    while (!kthread_should_stop()) {
        int num = sprintf(buf, "drv message test: count = %d\n", count++);
        log_fifo_write(buf, num);
        msleep(50);
    }
    pr_info("lkb log test thread-\n");
    return 0;
}
#endif

//static int __init my_module_init(void) {
int oplus_wlan_log_init(void) {
    int ret;

    log_drv_ready = false;

    //__kfifo_alloc
    fifo_buffer = kmalloc(LOG_KFIFO_SIZE, GFP_KERNEL);
    if (!fifo_buffer) {
        printk(KERN_ERR "alloc kfifo mem failed!");
        return -ENOMEM;
    }
    ret = kfifo_init(&log_fifo, fifo_buffer, LOG_KFIFO_SIZE);
    if (ret) {
        printk(KERN_ERR "kfifo init failed!\n");
        kfree(fifo_buffer);
        return ret;
    }

    init_waitqueue_head(&read_queue);

    ret = alloc_chrdev_region(&log_dev_num, 0, 1, "oplus_log");
    if (ret) {
        printk(KERN_ERR "Failed to allocate char device region\n");
        return ret;
    }

    cdev_init(&log_cdev, &fops);
    ret = cdev_add(&log_cdev, log_dev_num, 1);
    if (ret) {
        printk(KERN_ERR "Failed to add char device\n");
        unregister_chrdev_region(log_dev_num, 1);
        return ret;
    }

    cls = class_create(THIS_MODULE, "oplus_wlan_log_class");
    if (IS_ERR(cls)) {
        cdev_del(&log_cdev);
        unregister_chrdev_region(log_dev_num, 1);
        return PTR_ERR(cls);
    }

    device_create(cls, NULL, log_dev_num, NULL, "oplus_wlan_log");

    #ifdef THREAD_WRITE_LOG_TEST
    send_thread = kthread_run(thread_log_write_test, NULL, "log_thread_test");
    if (IS_ERR(send_thread)) {
        printk(KERN_ERR "Failed to create send thread\n");
    }
    #endif

    open_count = 0;
    log_drv_ready = true;

    return 0;
}

//static void __exit my_module_exit(void) {
void oplus_wlan_log_deinit(void) {
    #ifdef THREAD_WRITE_LOG_TEST
    kthread_stop(send_thread);
    #endif
    device_destroy(cls, log_dev_num);
    class_destroy(cls);
    cdev_del(&log_cdev);
    unregister_chrdev_region(log_dev_num, 1);
    kfifo_free(&log_fifo); //also free mem
}

//module_init(my_module_init);
//module_exit(my_module_exit);

//MODULE_LICENSE("GPL");