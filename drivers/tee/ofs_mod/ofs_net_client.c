#include <ofs/ofs_net.h>
#include <linux/slab.h>
#include <ofs/ofs_msg.h>
#include <linux/time.h>


#define PORT		2325
#define BUFSIZE		1024

#define BATCH_SIZE	0x1
#define ALL_BUFFERD 0


struct socket *conn_socket = NULL;
static int batch_cnt = 0;

/* unsigned char destip[5] = {10,42,1,1,'\0'}; [> fortwayne USB IP addr <] */
/* unsigned char destip[5] = {128,46,76,40,'\0'}; [> fortwayne USB IP addr <] */
unsigned char destip[5] = {128,46,76,31,'\0'}; /* fortwayne USB IP addr */


int ofs_cloud_bio_del_all(void) {
	struct list_head *pos, *q;
	struct ofs_cloud_bio *tmp;
	ofs_printk("%s:%d:deleting all entries in the list...\n", __func__, __LINE__);
	list_for_each_safe(pos, q, &ofs_cloud_bio_list) {
		tmp = list_entry(pos, struct ofs_cloud_bio, list);
		list_del(pos);
		kfree(tmp);
	}
}

u32 create_address(u8 *ip)
{
        u32 addr = 0;
        int i;

        for(i=0; i<4; i++)
        {
                addr += ip[i];
                if(i==3)
                        break;
                addr <<= 8;
        }
        return addr;
}

static int tcp_client_send(struct socket *sock, const char *buf, const size_t length,\
                unsigned long flags)
{
        struct msghdr msg;
        //struct iovec iov;
        struct kvec vec;
        int len, written = 0, left = length;
        mm_segment_t oldmm;

        msg.msg_name    = 0;
        msg.msg_namelen = 0;
        /*
        msg.msg_iov     = &iov;
        msg.msg_iovlen  = 1;
        */
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_flags   = flags;

        /* oldmm = get_fs(); set_fs(KERNEL_DS); */
repeat_send:
        /*
        msg.msg_iov->iov_len  = left;
        msg.msg_iov->iov_base = (char *)buf + written;
        */
        vec.iov_len = left;
        vec.iov_base = (char *)buf + written;

        //len = sock_sendmsg(sock, &msg, left);
        len = kernel_sendmsg(sock, &msg, &vec, 1, left);

        if((len == -ERESTARTSYS) || (!(flags & MSG_DONTWAIT) &&\
                                (len == -EAGAIN)))
                goto repeat_send;
        if(len > 0 && (len != length)) {
			ofs_printk("lwg:%s:%d:left overs!\n", __func__, __LINE__);
                written += len;
                left -= len;
                if(left)
                        goto repeat_send;
        }
        /* set_fs(oldmm); */
        return written ? written:len;
}

static int tcp_client_receive(struct socket *sock, char *str,\
                        unsigned long flags)
{
        struct msghdr msg;
        //struct iovec iov;
        struct kvec vec;
        int len;
		int ret, blknr, rw;
        int max_size = BUFSIZE;
		char *tok;

        msg.msg_name    = 0;
        msg.msg_namelen = 0;
        /*
        msg.msg_iov     = &iov;
        msg.msg_iovlen  = 1;
        */
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_flags   = flags;
        /*
        msg.msg_iov->iov_base   = str;
        msg.msg_ioc->iov_len    = max_size;
        */
        vec.iov_len = max_size;
        vec.iov_base = str;

read_again:
        //len = sock_recvmsg(sock, &msg, max_size, 0);
        len = kernel_recvmsg(sock, &msg, &vec, 1, 5, flags);

        if(len == -EAGAIN || len == -ERESTARTSYS)
        {
                /* pr_info(" *** mtp | error while reading: %d | " */
                /*         "tcp_client_receive *** \n", len); */
                goto read_again;
        } else if (len == 0) {
			printk("len = 0.....read again...\n");
			goto read_again;
		}
		ofs_printk("lwg:receiving %s\n", str);
		/* TODO: parse multiple */
		tok = strsep(&str, " ");
		while(tok != NULL) {
			/* printk("lwg:%s:%d:tok = %s\n", __func__, __LINE__, tok); */
			ret = sscanf(tok,"%08x,%d ", &blknr, &rw);
			if (ret == 2) { /* block request */
				struct ofs_cloud_bio *bio = kmalloc(sizeof(struct ofs_cloud_bio), GFP_KERNEL);
				/* printk("lwg:%s:%d:receiving [%08x, %d]\n", __func__, __LINE__, blknr, rw); */
				bio->blk = blknr;
				bio->rw  = rw;
				/* TODO: protect the list */
				list_add(&bio->list, &ofs_cloud_bio_list);
				if (!list_empty(&ofs_cloud_bio_list)) {
					struct ofs_cloud_bio *e;
					list_for_each_entry(e, &ofs_cloud_bio_list, list) {
						ofs_printk("lwg:%s:%d:blknr = %x\n", __func__, __LINE__, e->blk);
					}
				}
			} else {
				break;
			}
			tok = strsep(&str, " ");
		}
        return len;
}

static int tcp_client_connect(void)
{
        struct sockaddr_in saddr;
        /*
        struct sockaddr_in daddr;
        struct socket *data_socket = NULL;
        */
        /*
        char *response = kmalloc(4096, GFP_KERNEL);
        char *reply = kmalloc(4096, GFP_KERNEL);
        */
        int len = 49;
        char response[len+1];
        char reply[len+1];
        int ret = -1;

        //DECLARE_WAITQUEUE(recv_wait, current);
        DECLARE_WAIT_QUEUE_HEAD(recv_wait);

        ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &conn_socket);
        if(ret < 0)
        {
                pr_info(" *** mtp | Error: %d while creating first socket. | "
                        "setup_connection *** \n", ret);
                goto err;
        }

        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(PORT);
        saddr.sin_addr.s_addr = htonl(create_address(destip));

        ret = conn_socket->ops->connect(conn_socket, (struct sockaddr *)&saddr\
                        , sizeof(saddr), O_RDWR);
        if(ret && (ret != -EINPROGRESS))
        {
                pr_info(" *** mtp | Error: %d while connecting using conn "
                        "socket. | setup_connection *** \n", ret);
                goto err;
        }

        memset(&reply, 0, len+1);
        strcat(reply, "HOLA: OFS client initializing...\n");
        ret = tcp_client_send(conn_socket, reply, strlen(reply), MSG_DONTWAIT);
		ofs_printk("lwg:%s:%d:sending out [%d] bytes\n", __func__, __LINE__, ret);
        tcp_client_receive(conn_socket, response, MSG_DONTWAIT);
#if 0
        wait_event_timeout(recv_wait,\
                        !skb_queue_empty(&conn_socket->sk->sk_receive_queue),\
                                                                        5*HZ);
        /*
        add_wait_queue(&conn_socket->sk->sk_wq->wait, &recv_wait);
        while(1)
        {
                __set_current_status(TASK_INTERRUPTIBLE);
                schedule_timeout(HZ);
        */
                if(!skb_queue_empty(&conn_socket->sk->sk_receive_queue))
                {
                        /*
                        __set_current_status(TASK_RUNNING);
                        remove_wait_queue(&conn_socket->sk->sk_wq->wait,\
                                                              &recv_wait);
                        */
                        memset(&response, 0, len+1);
                        tcp_client_receive(conn_socket, response, MSG_DONTWAIT);
                        //break;
                }

        /*
        }
        */

        /* ret = tcp_client_send(conn_socket, reply, strlen(reply), MSG_DONTWAIT); */
#endif
err:
        return ret;
}


static void test_ofs_client_send(void) {
	struct ofs_fs_request tmp = {
		.request = OFS_OPEN,
		.count = 10,
		.fd = 0,
		.filename = "/mnt/ext2/test.txt"
	};
	int ret;
	char *fs_op = kmalloc(MAX_FILENAME, GFP_KERNEL);
	/* tcp_client_send(conn_socket, "lwg testing 1", 36, MSG_DONTWAIT); [> this is as far as it goes... <] */
	ret = serialize_ofs_fs_ops(&tmp, fs_op);
	ofs_printk("lwg:testing ofs fs op send...\n");
	ofs_printk("lwg:sending [%s]..\n", fs_op);
	tcp_client_send(conn_socket, fs_op, strlen(fs_op), MSG_DONTWAIT);
	/* tcp_client_send(conn_socket, "lwg testing 2", 36, MSG_DONTWAIT); */
	kfree(fs_op);
}

static void ofs_set_msg_flag(int req, unsigned long *flag) {
	if (req == OFS_FSYNC) {
		*flag = MSG_WAITALL;
	} else if (req == OFS_MMAP) {
		*flag = MSG_WAITALL;
	} else {
		*flag = MSG_DONTWAIT;
	}
	*flag = MSG_DONTWAIT;
}

int ofs_fs_send(struct ofs_fs_request *req) {
	int ret;
	char *reply = kmalloc(BUFSIZE, GFP_KERNEL);
	char *fs_op = kmalloc(BUFSIZE, GFP_KERNEL);
	struct timespec start, end, diff;
	unsigned long flag;
	/* not an fsync... */
	if (req->request != OFS_FSYNC) {
		if (batch_cnt++ < BATCH_SIZE) {
			return 0;
		} else {
			/* reset batch counter */
			batch_cnt = 0;
		}
	} else {
		printk("%s:fsync...\n", __func__);
	}
	ofs_set_msg_flag(req->request, &flag);
	memset(reply, 0x0, BUFSIZE);
	ret = serialize_ofs_fs_ops(req, fs_op);
	pr_info("lwg:%s:sending [%s].., flag = %lx\n", __func__, fs_op, flag);
	getnstimeofday(&start);
	/* tcp_client_send(conn_socket, fs_op, strlen(fs_op), MSG_DONTWAIT); */
	tcp_client_send(conn_socket, fs_op, strlen(fs_op), flag);
	kfree(fs_op);
	/* always expecting to see the response from the cloud */
	/* ret = tcp_client_receive(conn_socket, reply, MSG_DONTWAIT); */
	ret = tcp_client_receive(conn_socket, reply, flag);
	getnstimeofday(&end);
	diff = timespec_sub(end, start);
	if (ret) {
		printk("lwg:%s:%d:receiving bio from the cloud -- [%s]\n", __func__, __LINE__, reply);
		printk("rtt = %lld s, %lld ns\n", diff.tv_sec, diff.tv_nsec);
		kfree(reply);
	}
}



int ofs_network_client_init(void)
{
        tcp_client_connect();
		/* test_ofs_client_send(); */
		printk("lwg:%s:network client initialized!\n", __func__);
        return 0;
}

void network_client_exit(void)
{
        int len = 49;
        char response[len+1];
        char reply[len+1];

        //DECLARE_WAITQUEUE(exit_wait, current);
        DECLARE_WAIT_QUEUE_HEAD(exit_wait);

        memset(&reply, 0, len+1);
        strcat(reply, "ADIOS");
        //tcp_client_send(conn_socket, reply);
        tcp_client_send(conn_socket, reply, strlen(reply), MSG_DONTWAIT);

        //while(1)
        //{
                /*
                tcp_client_receive(conn_socket, response);
                add_wait_queue(&conn_socket->sk->sk_wq->wait, &exit_wait)
                */
         wait_event_timeout(exit_wait,\
                         !skb_queue_empty(&conn_socket->sk->sk_receive_queue),\
                                                                        5*HZ);
        if(!skb_queue_empty(&conn_socket->sk->sk_receive_queue))
        {
                memset(&response, 0, len+1);
                tcp_client_receive(conn_socket, response, MSG_DONTWAIT);
                //remove_wait_queue(&conn_socket->sk->sk_wq->wait, &exit_wait);
        }

        //}

        if(conn_socket != NULL)
        {
                sock_release(conn_socket);
        }
        pr_info(" *** mtp | network client exiting | network_client_exit *** \n");
}

