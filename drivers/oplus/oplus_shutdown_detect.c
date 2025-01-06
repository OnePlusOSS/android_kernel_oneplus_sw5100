// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/**********************************************************************************
* Description:     shutdown_detect Monitor  Kernel Driver
*
* Version   : 1.0
***********************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>
#include <linux/kbd_kern.h>
#include <linux/proc_fs.h>
#include <linux/nmi.h>
#include <linux/quotaops.h>
#include <linux/perf_event.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/writeback.h>
#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/vt_kern.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/oom.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/uaccess.h>
#include <linux/moduleparam.h>
#include <linux/jiffies.h>
#include <linux/syscalls.h>
#include <linux/of.h>
#include <linux/rcupdate.h>
#include <linux/kthread.h>

#include <asm/ptrace.h>
#include <asm/irq_regs.h>


#include <linux/sysrq.h>
#include <linux/clk.h>

#include <linux/kmsg_dump.h>
#include <linux/version.h>

#include <linux/reboot.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <linux/timekeeping.h>
#endif

#include <include/oplus_project.h>

#include <linux/blkdev.h>
#define SEQ_printf(m, x...)     \
    do {                        \
        if (m)                  \
            seq_printf(m, x);   \
        else                    \
            pr_debug(x);        \
    } while (0)


#define OPLUS_SHUTDOWN_LOG_START_BLOCK_EMMC  10240
#define OPLUS_SHUTDOWN_LOG_START_BLOCK_UFS   1280
#define OPLUS_SHUTDOWN_KERNEL_LOG_SIZE_BYTES 1024 * 1024
#define OPLUS_SHUTDOWN_FLAG_OFFSET           0 * 1024 * 1024
#define OPLUS_SHUTDOWN_KMSG_OFFSET           61 * 1024 * 1024
#define FILE_MODE_0666                      0666

#define BLOCK_SIZE_EMMC                     512
#define BLOCK_SIZE_UFS                      4096

#define SHUTDOWN_MAGIC_LEN                  8

#define ShutDownTO                          0x9B

#define TASK_INIT_COMM                      "init"

#define OPLUS_PARTITION_OPLUSRESERVE3_LINK    "/dev/block/by-name/oplusreserve3"

#define SIG_SHUTDOWN                        (SIGRTMIN + 0x12)

#define SHUTDOWN_STAGE_KERNEL               20
#define SHUTDOWN_STAGE_INIT                 30
#define SHUTDOWN_STAGE_SYSTEMSERVER         40
#define SHUTDOWN_TIMEOUNT_UMOUNT            31
#define SHUTDOWN_TIMEOUNT_VOLUME            32
#define SHUTDOWN_TIMEOUNT_SUBSYSTEM         43
#define SHUTDOWN_TIMEOUNT_RADIOS            44
#define SHUTDOWN_TIMEOUNT_PM                45
#define SHUTDOWN_TIMEOUNT_AM                46
#define SHUTDOWN_TIMEOUNT_BC                47
#define SHUTDOWN_STAGE_INIT_POFF            70
#define SHUTDOWN_RUS_MIN                    255
#define SHUTDOWN_TOTAL_TIME_MIN             60
#define SHUTDOWN_DEFAULT_NATIVE_TIME        15
#define SHUTDOWN_DEFAULT_JAVA_TIME          15
#define SHUTDOWN_DEFAULT_TOTAL_TIME         150 //default 90, modify for storage stability testing
#define SHUTDOWN_INCREASE_TIME              5

#define KE_LOG_COLLECT_TIMEOUT              msecs_to_jiffies(10000)

#define RETRY_COUNT_FOR_GET_DEVICE 50
#define WAITING_FOR_GET_DEVICE     100
#define BUF_SIZE_OF_LINE           2048
#define WB_BLOCK_SIZE              4096
#define USE_IMMEDIATE

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0)
static int blkdev_fsync(struct file *filp, loff_t start, loff_t end, int datasync);
#endif

const struct file_operations f_op  = {.fsync = blkdev_fsync};

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0)
static struct kmsg_dump_iter shutdown_kmsg_dumper;
#else
static struct kmsg_dumper    shutdown_kmsg_dumper;
#endif

struct block_device *get_reserve_partition_bdev(void)
{
    struct block_device *bdev = NULL;
    int retry_wait_for_device = RETRY_COUNT_FOR_GET_DEVICE;
    dev_t dev;

    while(retry_wait_for_device--) {
        dev = name_to_dev_t("PARTLABEL=oplusreserve3");
        if(dev != 0) {
            bdev = blkdev_get_by_dev(dev, FMODE_READ | FMODE_WRITE | FMODE_EXCL, THIS_MODULE);
            pr_err("[shd] success to get dev block\n");
            return bdev;
        }
        pr_err("[shd] Failed to get dev block, retry %d\n", retry_wait_for_device);
        msleep_interruptible(WAITING_FOR_GET_DEVICE);
    }
    pr_err("[shd] Failed to get dev block final\n");
    return NULL;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0)
static int blkdev_fsync(struct file *filp, loff_t start, loff_t end,
        int datasync)
{
    struct inode *bd_inode = filp->f_mapping->host;
    struct block_device *bdev = I_BDEV(bd_inode);
    int error;

    error = file_write_and_wait_range(filp, start, end);
    if (error)
        return error;

    /*
     * There is no need to serialise calls to blkdev_issue_flush with
     * i_mutex and doing so causes performance issues with concurrent
     * O_SYNC writers to a block device.
     */
    error = blkdev_issue_flush(bdev);
    if (error == -EOPNOTSUPP)
        error = 0;

    return error;
}
#endif

static DECLARE_COMPLETION(shd_comp);
static DEFINE_MUTEX(shd_wf_mutex);

static unsigned int shutdown_phase;
static bool shutdown_detect_started = false;
static bool shutdown_detect_enable = true;
static bool is_shutdows = false;
static unsigned int gtimeout = 0;
static unsigned int gtotaltimeout = SHUTDOWN_DEFAULT_TOTAL_TIME;
static unsigned int gjavatimeout = SHUTDOWN_DEFAULT_JAVA_TIME;
static unsigned int gnativetimeout = SHUTDOWN_DEFAULT_NATIVE_TIME;

static struct task_struct *shutdown_task = NULL;
struct task_struct *shd_complete_monitor = NULL;


extern int oplus_enter_shipmode(void);
extern int oplus_enable_ship_mode_flag;

struct shd_info {
    char magic[SHUTDOWN_MAGIC_LEN];
    int  shutdown_err;
    int  shutdown_times;
};

#define SIZEOF_STRUCT_SHD_INFO sizeof(struct shd_info)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
time_t shutdown_start_time = 0;
time_t shutdown_end_time = 0;
time_t shutdown_systemserver_start_time = 0;
time_t shutdown_init_start_time = 0;
time_t shutdown_kernel_start_time = 0;
#else
time64_t shutdown_start_time = 0;
time64_t shutdown_end_time = 0;
time64_t shutdown_systemserver_start_time = 0;
time64_t shutdown_init_start_time = 0;
time64_t shutdown_kernel_start_time = 0;
#endif

static int shutdown_kthread(void *data)
{
    kernel_power_off();
    return 0;
}

static int shutdown_detect_func(void *dummy);

static void shutdown_timeout_flag_write(int timeout);
static void shutdown_dump_kernel_log(void);
static int shutdown_timeout_flag_write_now(void *args);

extern int creds_change_dac(void);
extern int shutdown_kernel_log_save(void *args);
extern void shutdown_dump_android_log(void);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static struct timespec current_kernel_time(void)
{
    struct timespec64 ts64;

    ktime_get_real_ts64(&ts64);

    return timespec64_to_timespec(ts64);
}
#else
static time64_t current_kernel_time(void)
{
    struct timespec64 ts64;
    ktime_get_real_ts64(&ts64);
    return ts64.tv_sec;
}
#endif

static ssize_t shutdown_detect_trigger(struct file *filp, const char *ubuf,
                       size_t cnt, loff_t *data)
{
    char buf[64];
    long val = 0;
    int ret = 0;
    struct task_struct *tsk = NULL;
    unsigned int temp = SHUTDOWN_DEFAULT_TOTAL_TIME;

    if(shutdown_detect_enable == false) {
        return -EPERM;
    }

    if (cnt >= sizeof(buf)) {
        return -EINVAL;
    }

    if (copy_from_user(&buf, ubuf, cnt)) {
        return -EFAULT;
    }

    buf[cnt] = 0;

    ret = kstrtoul(buf, 0, (unsigned long *)&val);

    if (ret < 0) {
        return ret;
    }

    if (val == SHUTDOWN_STAGE_INIT_POFF) {
         is_shutdows = true;
         val = SHUTDOWN_STAGE_INIT;
    }
    gnativetimeout += SHUTDOWN_INCREASE_TIME;
    gjavatimeout += SHUTDOWN_INCREASE_TIME;
/*wen.luo@PSW.BSP.Kernel.Stability. 2019/07/11, modify for show the murderer*/
    tsk = current->group_leader;
    pr_info("%s:%d shutdown_detect, GroupLeader is %s:%d\n", current->comm,
        task_pid_nr(current), tsk->comm, task_pid_nr(tsk));
    //val: 0x gtotaltimeout|gjavatimeout|gnativetimeout , gnativetimeout < F, gjavatimeout < F
    if (val > SHUTDOWN_RUS_MIN) {
         gnativetimeout = val % 16;
         gjavatimeout = ((val - gnativetimeout) % 256 ) / 16;
         temp = val / 256;
        gtotaltimeout = (temp < SHUTDOWN_TOTAL_TIME_MIN) ?
                    SHUTDOWN_TOTAL_TIME_MIN :
                    temp;
        pr_info("shutdown_detect_trigger rus val %ld %d %d %d\n", val,
            gnativetimeout, gjavatimeout, gtotaltimeout);
         return cnt;
    }

    //pr_err("shutdown_detect_trigger final val %ld %d %d %d\n", val, gnativetimeout, gjavatimeout, gtotaltimeout);

    switch (val) {
    case 0:
        if (shutdown_detect_started) {
            shutdown_detect_started = false;
            shutdown_phase = 0;
        }
        shutdown_detect_enable = false;
        pr_err("shutdown_detect: abort shutdown detect\n");
        break;
    case SHUTDOWN_STAGE_KERNEL:
        pr_err("shutdown_detect:%d\n",oplus_enable_ship_mode_flag);
        if(oplus_enable_ship_mode_flag) {
            oplus_enter_shipmode();
        }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
        shutdown_kernel_start_time = current_kernel_time().tv_sec;
#else
        shutdown_kernel_start_time = current_kernel_time();
#endif
        //Ansheng Ma@OPLUS.com Android.Framework.Stability every reboot create shutdownlog fatal
        if ((shutdown_init_start_time < shutdown_kernel_start_time)
            &&(shutdown_kernel_start_time - shutdown_init_start_time) > gnativetimeout) {
            pr_err("shutdown_detect_timeout: timeout happened in reboot.cpp (%ld-%ld > %u)\n",
                shutdown_kernel_start_time, shutdown_init_start_time, gnativetimeout);
            shutdown_dump_kernel_log();
            shutdown_timeout_flag_write(1);
        } else {
            if (gjavatimeout < SHUTDOWN_DEFAULT_JAVA_TIME) {
                pr_err("shutdown_detect_timeout: gjavatimeout(%ud) is too small\n", gjavatimeout);
                gjavatimeout = SHUTDOWN_DEFAULT_JAVA_TIME + SHUTDOWN_INCREASE_TIME;
            }

            if ((shutdown_systemserver_start_time < shutdown_init_start_time)
                &&((shutdown_init_start_time - shutdown_systemserver_start_time) > gjavatimeout)
                && shutdown_systemserver_start_time) {
                // timeout happend in system_server stage
                pr_err("shutdown_detect_timeout: %ld - %ld > %u\n",
                    shutdown_init_start_time, shutdown_systemserver_start_time, gjavatimeout);
                shutdown_timeout_flag_write(1);
            }
        }
        shutdown_phase = val;
        pr_err("shutdown_detect_phase: shutdown  current phase systemcall\n");
        break;
    case SHUTDOWN_STAGE_INIT:
        if (!shutdown_detect_started) {
            shutdown_detect_started = true;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
            shutdown_init_start_time = current_kernel_time().tv_sec;
#else
            shutdown_init_start_time = current_kernel_time();
#endif
            shutdown_start_time = shutdown_init_start_time;
            shd_complete_monitor = kthread_run(shutdown_detect_func, NULL, "shutdown_detect_thread");
            if (IS_ERR(shd_complete_monitor)) {
                ret = PTR_ERR(shd_complete_monitor);
                pr_err("shutdown_detect: cannot start thread: %d\n", ret);
            }

        } else {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
            shutdown_init_start_time = current_kernel_time().tv_sec;
#else
            shutdown_init_start_time = current_kernel_time();
#endif

            if (gjavatimeout < SHUTDOWN_DEFAULT_JAVA_TIME) {
                pr_err("shutdown_detect_timeout: gjavatimeout(%ud) is too small\n", gjavatimeout);
                gjavatimeout = SHUTDOWN_DEFAULT_JAVA_TIME + SHUTDOWN_INCREASE_TIME;
            }

            if ((shutdown_systemserver_start_time < shutdown_init_start_time)
                &&(shutdown_init_start_time - shutdown_systemserver_start_time) > gjavatimeout) {
                pr_err("shutdown_detect_timeout: timeout happened in system_server stage.(%ld,%ld,%u)\n",
                    shutdown_init_start_time, shutdown_systemserver_start_time, gjavatimeout);
                shutdown_dump_android_log();
                shutdown_dump_kernel_log();
            }
        }
        //pr_err("shutdown_init_start_time %ld\n", shutdown_init_start_time);
        shutdown_phase = val;
        pr_err("shutdown_detect_phase: shutdown  current phase init\n");
        break;
    case SHUTDOWN_TIMEOUNT_UMOUNT:
        pr_err("shutdown_detect_timeout: umount timeout\n");
        break;
    case SHUTDOWN_TIMEOUNT_VOLUME:
        pr_err("shutdown_detect_timeout: volume shutdown timeout\n");
        break;
    case SHUTDOWN_STAGE_SYSTEMSERVER:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
        shutdown_systemserver_start_time = current_kernel_time().tv_sec;
#else
        shutdown_systemserver_start_time = current_kernel_time();
#endif

        //pr_err("shutdown_systemserver_start_time %ld\n", shutdown_systemserver_start_time);
        if (!shutdown_detect_started) {
            shutdown_detect_started = true;
            shutdown_start_time = shutdown_systemserver_start_time;
            shd_complete_monitor =
                kthread_run(shutdown_detect_func, NULL,
                        "shutdown_detect_thread");
        }
        shutdown_phase = val;
        pr_err("shutdown_detect_phase: shutdown  current phase systemserver\n");
        break;
    case SHUTDOWN_TIMEOUNT_SUBSYSTEM:
        pr_err("shutdown_detect_timeout: ShutdownSubSystem timeout\n");
        break;
    case SHUTDOWN_TIMEOUNT_RADIOS:
        pr_err("shutdown_detect_timeout: ShutdownRadios timeout\n");
        break;
    case SHUTDOWN_TIMEOUNT_PM:
        pr_err("shutdown_detect_timeout: ShutdownPackageManager timeout\n");
        break;
    case SHUTDOWN_TIMEOUNT_AM:
        pr_err("shutdown_detect_timeout: ShutdownActivityManager timeout\n");
        break;
    case SHUTDOWN_TIMEOUNT_BC:
        pr_err("shutdown_detect_timeout: SendShutdownBroadcast timeout\n");
        break;
    default:
        break;
    }
    if(!shutdown_task && is_shutdows) {
        shutdown_task = kthread_create(shutdown_kthread, NULL,
                           "shutdown_kthread");
        if (IS_ERR(shutdown_task)) {
            pr_err("create shutdown thread fail, will BUG()\n");
            msleep(60*1000);
            BUG();
        }
    }
    return cnt;
}

static int shutdown_detect_show(struct seq_file *m, void *v)
{
    SEQ_printf(m, "=== shutdown_detect controller ===\n");
    SEQ_printf(m, "0:   shutdown_detect abort\n");
    SEQ_printf(m, "20:   shutdown_detect systemcall reboot phase\n");
    SEQ_printf(m, "30:   shutdown_detect init reboot phase\n");
    SEQ_printf(m, "40:   shutdown_detect system server reboot phase\n");
    SEQ_printf(m, "=== shutdown_detect controller ===\n\n");
    SEQ_printf(m, "shutdown_detect: shutdown phase: %u\n", shutdown_phase);
    return 0;
}

static int shutdown_detect_open(struct inode *inode, struct file *file)
{
    return single_open(file, shutdown_detect_show, inode->i_private);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations shutdown_detect_fops = {
    .open        = shutdown_detect_open,
    .write       = shutdown_detect_trigger,
    .read        = seq_read,
    .llseek      = seq_lseek,
    .release     = single_release,
};
#else
static const struct proc_ops shutdown_detect_fops = {
    .proc_open = shutdown_detect_open,
    .proc_write = shutdown_detect_trigger,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#endif

static int dump_kmsg(struct block_device *bdev, char *buf, char *line_buf)
{
    struct file dev_map_file;
    struct kvec iov;
    struct iov_iter iter;
    struct kiocb kiocb;

    bool isfull = false;
    int tmp_size = 0;
    size_t len = 0;
    int ret = 0;
    memset(&dev_map_file, 0, sizeof(struct file));
    dev_map_file.f_mapping = bdev->bd_inode->i_mapping;
    dev_map_file.f_flags = O_DSYNC | __O_SYNC | O_NOATIME;
    dev_map_file.f_inode = bdev->bd_inode;
    init_sync_kiocb(&kiocb, &dev_map_file);
    kiocb.ki_pos = OPLUS_SHUTDOWN_KMSG_OFFSET; //start wb offset
    memset(buf, 0, WB_BLOCK_SIZE);
    pr_info("[shutdown_detect] Start dump_kmsg\n");
    while (kmsg_dump_get_line(&shutdown_kmsg_dumper, true, line_buf, BUF_SIZE_OF_LINE, &len)) {
        if((tmp_size + len) >= WB_BLOCK_SIZE) {
            isfull = true;
        }
        iov.iov_base = (void *)buf;
        iov.iov_len = tmp_size;
        iov_iter_kvec(&iter, WRITE, &iov, 0, tmp_size);
        ret = generic_write_checks(&kiocb, &iter);
        if (likely(ret > 0))
            ret = generic_perform_write(&dev_map_file, &iter, kiocb.ki_pos);
        else {
            pr_err("generic_write_checks failed\n");
            return 1;
        }
        if (likely(ret > 0)) {
            dev_map_file.f_op = &f_op;
            if(isfull) {
                kiocb.ki_pos += tmp_size; //shfit offset of write back size
            }
            ret = generic_write_sync(&kiocb, ret);
            if (ret < 0) {
                pr_err("Write sync failed\n");
                return 1;
            }
        } else {
            pr_err("generic_perform_write failed\n");
            return 1;
        }
        isfull = false;

        memcpy(buf + tmp_size, line_buf, len);
        tmp_size += len;
        memset(line_buf, 0, BUF_SIZE_OF_LINE);
        len = 0;
  }
    return 0;
}

int shutdown_kernel_log_save(void *args)
{
    struct block_device *bdev = NULL;
    char *data_buf = NULL;
    char *line_buf = NULL;
    bdev = get_reserve_partition_bdev();
    if (!bdev) {
        pr_err("[shutdown_detect] get device oplusreserve3 failed\n");
        goto EXIT;
    }
    data_buf = (char *)kzalloc(WB_BLOCK_SIZE * sizeof(char), GFP_KERNEL);
    if (!data_buf) {
        pr_err("shutdown_detect] Alloc data_buf failed\n");
        goto EXIT;
    }
    line_buf = (char *)kzalloc(BUF_SIZE_OF_LINE * sizeof(char), GFP_KERNEL);
    if (!line_buf) {
        pr_err("shutdown_detect] Alloc line_buf failed\n");
        goto EXIT;
    }
    if (0 != dump_kmsg(bdev, data_buf, line_buf)) {
        pr_err("[shutdown_detect] dump kmsg to PARTITION_RESERVE3_LINK failed\n");
        goto EXIT;
    }

    complete(&shd_comp);
    return 1;

EXIT:
    if (data_buf) {
        kfree(data_buf);
        data_buf = NULL;
        pr_info("[SHD] Free data_buf buffer\n");
    }

    if (line_buf) {
        kfree(line_buf);
        line_buf = NULL;
        pr_info("[SHD]Free line_buf buffer\n");
    }

    if (bdev) {
        blkdev_put(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
        bdev = NULL;
        pr_info("[SHD] Put device\n");
    }
    complete(&shd_comp);
    return -1;
}

static int shutdown_timeout_flag_write_now(void *args)
{
    struct block_device *bdev = NULL;
    struct file dev_map_file;
    struct kvec iov;
    struct iov_iter iter;
    struct kiocb kiocb;
    int ret = 0;
    char data_info[SIZEOF_STRUCT_SHD_INFO] = {'\0'};
    struct shd_info shutdown_flag;
    bdev = get_reserve_partition_bdev();
    memset(&dev_map_file, 0, sizeof(struct file));
      dev_map_file.f_mapping = bdev->bd_inode->i_mapping;
    dev_map_file.f_flags = O_DSYNC | __O_SYNC | O_NOATIME;
    dev_map_file.f_inode = bdev->bd_inode;
    init_sync_kiocb(&kiocb, &dev_map_file);
    kiocb.ki_pos = OPLUS_SHUTDOWN_FLAG_OFFSET; //start wb offset
    strncpy(shutdown_flag.magic, "ShutDown", SHUTDOWN_MAGIC_LEN);
    if(gtimeout) {
        shutdown_flag.shutdown_err = ShutDownTO;
    } else {
        shutdown_flag.shutdown_err = 0;
    }
    shutdown_flag.shutdown_times =
        (int)(shutdown_end_time - shutdown_start_time);

    memcpy(data_info, &shutdown_flag, SIZEOF_STRUCT_SHD_INFO);
    iov.iov_base = (void *)data_info;
    iov.iov_len = SIZEOF_STRUCT_SHD_INFO;
    iov_iter_kvec(&iter, WRITE, &iov, 0, SIZEOF_STRUCT_SHD_INFO);
    ret = generic_write_checks(&kiocb, &iter);
    if (likely(ret > 0))
        ret = generic_perform_write(&dev_map_file, &iter, kiocb.ki_pos);
    else {
        pr_err("generic_write_checks failed\n");
        return 1;
    }
    if (likely(ret > 0)) {
        dev_map_file.f_op = &f_op;
        ret = generic_write_sync(&kiocb, ret);
        if (ret < 0) {
            pr_err("Write sync failed\n");
            return 1;
        }
    } else {
        pr_err("generic_perform_write failed\n");
        return 1;
    }

    pr_info("shutdown_timeout_flag_write_now done \n");
    complete(&shd_comp);

    return 0;
}

static void task_comm_to_struct(const char *pcomm,
                struct task_struct **t_result)
{
    struct task_struct *g, *t;
    rcu_read_lock();
    for_each_process_thread(g, t) {
        if (!strcmp(t->comm, pcomm)) {
            *t_result = t;
            rcu_read_unlock();
            return;
        }
    }
    t_result = NULL;
    rcu_read_unlock();
}

void shutdown_dump_android_log(void)
{
    struct task_struct *sd_init;
    sd_init = NULL;
    task_comm_to_struct(TASK_INIT_COMM, &sd_init);
    if(NULL != sd_init) {
        pr_err("send shutdown_dump_android_log signal %d", SIG_SHUTDOWN);
        send_sig(SIG_SHUTDOWN, sd_init, 0);
        pr_err("after send shutdown_dump_android_log signal %d", SIG_SHUTDOWN);
        // wait to collect shutdown log finished
        schedule_timeout_interruptible(20 * HZ);
    }
}

static void shutdown_dump_kernel_log(void)
{
    struct task_struct *tsk;
    tsk = kthread_run(shutdown_kernel_log_save, NULL, "shd_collect_dmesg");
    if (IS_ERR(tsk)) {
        pr_err("create kernel thread shd_collect_dmesg failed\n");
        return;
    }
    // wait max 10s to collect shutdown log finished
    if (!wait_for_completion_timeout(&shd_comp, KE_LOG_COLLECT_TIMEOUT)) {
        pr_err("collect kernel log timeout\n");
    }
}

static void shutdown_timeout_flag_write(int timeout)
{
    struct task_struct *tsk;

    gtimeout = timeout;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
    shutdown_end_time = current_kernel_time().tv_sec;
#else
    shutdown_end_time = current_kernel_time();
#endif

    tsk = kthread_run(shutdown_timeout_flag_write_now, NULL, "shd_to_flag");
    if (IS_ERR(tsk)) {
        pr_err("create kernel thread shd_to_flag failed\n");
        return;
    }
    // wait max 10s to collect shutdown log finished
    if (!wait_for_completion_timeout(&shd_comp, KE_LOG_COLLECT_TIMEOUT)) {
        pr_err("shutdown_timeout_flag_write timeout\n");
    }
}

static int shutdown_detect_func(void *dummy)
{
    //schedule_timeout_uninterruptible(gtotaltimeout * HZ);
    msleep(gtotaltimeout * 1000);

    pr_err("shutdown_detect:%s call sysrq show block and cpu thread. BUG\n", __func__);
    handle_sysrq('w');
    handle_sysrq('l');
    pr_err("shutdown_detect:%s shutdown_detect status:%u. \n", __func__, shutdown_phase);

    pr_err("shutdown_detect: shutdown or reboot? %s\n", (is_shutdows)?"shutdown":"reboot");

    if(shutdown_phase >= SHUTDOWN_STAGE_INIT) {
        shutdown_dump_android_log();
    }

    shutdown_dump_kernel_log();

    shutdown_timeout_flag_write(1);// timeout happened

    if(is_shutdows){
        extern void kernel_force_power_off(void);
        pr_err("shutdown_detect: shutdown kernel_power_off()\n");
        if (shutdown_task)
            wake_up_process(shutdown_task);
        //else
          //  kernel_power_off();
        msleep(5000);
        pr_err("shutdown_detect: shutdown kernel_force_power_off()\n");
        //kernel_force_power_off();
    }else{
        //WARN_ON(1);
        pr_err("shutdown_detect: shutdown kernel_force_restart()\n");
        kernel_restart(NULL);
    }
    return 0;
}

static int __init init_shutdown_detect_ctrl(void)
{
    struct proc_dir_entry *pe;
    pr_err("shutdown_detect:register shutdown_detect interface\n");
    pe = proc_create("shutdown_detect", 0664, NULL, &shutdown_detect_fops);
    if (!pe) {
        pr_err("shutdown_detect:Failed to register shutdown_detect interface\n");
        return -ENOMEM;
    }
    return 0;
}

device_initcall(init_shutdown_detect_ctrl);

MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
MODULE_LICENSE("GPL v2");
