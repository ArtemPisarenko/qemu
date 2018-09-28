/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/replay.h"

#include "chardev/char-fe.h"
#include "chardev/char-io.h"
#include "chardev/char-mux.h"

#ifdef HACK_CHARDEV_SYNC
static void fe_generate_open_event(void *opaque)
{
	CharBackend *be = opaque;
	if (be && be->chr_event) {
		be->chr_event(be->opaque, CHR_EVENT_OPENED);
	}
}

void qemu_chr_fe_event(CharBackend *be, int event)
{
	if (!be || !be->chr_event) {
		return;
	}

#ifdef HACK_CHARDEV_SYNC
	if (be->fe_drop_openclose) {
		switch (event) {
		case CHR_EVENT_OPENED:
		case CHR_EVENT_CLOSED:
			return;
		default:
			break;
		}
	}
#endif
#ifdef HACK_CHARDEV_FE_DROP_INPUT
	if (be->fe_drop_input) {
		switch (event) {
		case CHR_EVENT_BREAK:
		case CHR_EVENT_MUX_IN:
		case CHR_EVENT_MUX_OUT:
			return;
		default:
			break;
		}
	}
#endif

	be->chr_event(be->opaque, event);
}
#endif /* HACK_CHARDEV_SYNC */

int qemu_chr_fe_write(CharBackend *be, const uint8_t *buf, int len)
{
    Chardev *s = be->chr;

    if (!s) {
        return 0;
    }

#ifdef HACK_CHARDEV_SYNC //TODO: make conditional
    return qemu_chr_write(s, buf, len, true);
#else
    return qemu_chr_write(s, buf, len, false);
#endif
}

int qemu_chr_fe_write_all(CharBackend *be, const uint8_t *buf, int len)
{
    Chardev *s = be->chr;

    if (!s) {
        return 0;
    }

    return qemu_chr_write(s, buf, len, true);
}

int qemu_chr_fe_read_all(CharBackend *be, uint8_t *buf, int len)
{
#ifdef HACK_CHARDEV_FE_DROP_INPUT // TODO: refactor and make conditional
	(void)be;
	(void)buf;
	(void)len;
	return 0;
#else
    Chardev *s = be->chr;
    int offset = 0, counter = 10;
    int res;

    if (!s || !CHARDEV_GET_CLASS(s)->chr_sync_read) {
        return 0;
    }

    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_PLAY) {
        return replay_char_read_all_load(buf);
    }

    while (offset < len) {
    retry:
        res = CHARDEV_GET_CLASS(s)->chr_sync_read(s, buf + offset,
                                                  len - offset);
        if (res == -1 && errno == EAGAIN) {
            g_usleep(100);
            goto retry;
        }

        if (res == 0) {
            break;
        }

        if (res < 0) {
            if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_RECORD) {
                replay_char_read_all_save_error(res);
            }
            return res;
        }

        offset += res;

        if (!counter--) {
            break;
        }
    }

    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_RECORD) {
        replay_char_read_all_save_buf(buf, offset);
    }
    return offset;
#endif /* HACK_CHARDEV_FE_DROP_INPUT */
}

int qemu_chr_fe_ioctl(CharBackend *be, int cmd, void *arg)
{
    Chardev *s = be->chr;
    int res;

    if (!s || !CHARDEV_GET_CLASS(s)->chr_ioctl || qemu_chr_replay(s)) {
        res = -ENOTSUP;
    } else {
        res = CHARDEV_GET_CLASS(s)->chr_ioctl(s, cmd, arg);
    }

    return res;
}

int qemu_chr_fe_get_msgfd(CharBackend *be)
{
    Chardev *s = be->chr;
    int fd;
    int res = (qemu_chr_fe_get_msgfds(be, &fd, 1) == 1) ? fd : -1;
    if (s && qemu_chr_replay(s)) {
        error_report("Replay: get msgfd is not supported "
                     "for serial devices yet");
        exit(1);
    }
    return res;
}

int qemu_chr_fe_get_msgfds(CharBackend *be, int *fds, int len)
{
    Chardev *s = be->chr;

    if (!s) {
        return -1;
    }

    return CHARDEV_GET_CLASS(s)->get_msgfds ?
        CHARDEV_GET_CLASS(s)->get_msgfds(s, fds, len) : -1;
}

int qemu_chr_fe_set_msgfds(CharBackend *be, int *fds, int num)
{
    Chardev *s = be->chr;

    if (!s) {
        return -1;
    }

    return CHARDEV_GET_CLASS(s)->set_msgfds ?
        CHARDEV_GET_CLASS(s)->set_msgfds(s, fds, num) : -1;
}

void qemu_chr_fe_accept_input(CharBackend *be)
{
    Chardev *s = be->chr;

    if (!s) {
        return;
    }

    if (CHARDEV_GET_CLASS(s)->chr_accept_input) {
        CHARDEV_GET_CLASS(s)->chr_accept_input(s);
    }
    qemu_notify_event();
}

void qemu_chr_fe_printf(CharBackend *be, const char *fmt, ...)
{
    char buf[CHR_READ_BUF_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    /* XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks */
    qemu_chr_fe_write_all(be, (uint8_t *)buf, strlen(buf));
    va_end(ap);
}

Chardev *qemu_chr_fe_get_driver(CharBackend *be)
{
    /* this is unsafe for the users that support chardev hotswap */
    assert(be->chr_be_change == NULL);
    return be->chr;
}

bool qemu_chr_fe_backend_connected(CharBackend *be)
{
    return !!be->chr;
}

bool qemu_chr_fe_backend_open(CharBackend *be)
{
    return be->chr && be->chr->be_open;
}

bool qemu_chr_fe_init(CharBackend *b, Chardev *s, Error **errp)
{
    int tag = 0;

    if (s) {
        if (CHARDEV_IS_MUX(s)) {
            MuxChardev *d = MUX_CHARDEV(s);

            if (d->mux_cnt >= MAX_MUX) {
                goto unavailable;
            }

            d->backends[d->mux_cnt] = b;
            tag = d->mux_cnt++;
        } else if (s->be) {
            goto unavailable;
        } else {
            s->be = b;
        }
    }

#ifdef HACK_CHARDEV_SYNC
    b->chr_can_read = NULL;
    b->chr_read = NULL;
    b->chr_event = NULL;
    b->fe_drop_openclose = false;
    b->fe_deffered_open_bh = qemu_bh_new(fe_generate_open_event, b);
#ifdef HACK_CHARDEV_FE_DROP_INPUT //TODO: make conditional
    b->fe_drop_input = true;
#endif
#endif /* HACK_CHARDEV_SYNC */

    b->fe_open = false;
    b->tag = tag;
    b->chr = s;
    return true;

unavailable:
    error_setg(errp, QERR_DEVICE_IN_USE, s->label);
    return false;
}

void qemu_chr_fe_deinit(CharBackend *b, bool del)
{
    assert(b);

#ifdef HACK_CHARDEV_SYNC
    qemu_bh_delete(b->fe_deffered_open_bh);
#endif

    if (b->chr) {
        qemu_chr_fe_set_handlers(b, NULL, NULL, NULL, NULL, NULL, NULL, true);
        if (b->chr->be == b) {
            b->chr->be = NULL;
        }
        if (CHARDEV_IS_MUX(b->chr)) {
            MuxChardev *d = MUX_CHARDEV(b->chr);
            d->backends[b->tag] = NULL;
        }
        if (del) {
            object_unparent(OBJECT(b->chr));
        }
        b->chr = NULL;
    }
}

#ifdef HACK_CHARDEV_SYNC
void qemu_chr_fe_mark_non_guest_device(CharBackend *b)
{
    assert(!(b->chr_can_read || b->chr_read));
#ifdef HACK_CHARDEV_FE_DROP_INPUT //TODO: synchronize with option value assignment
    b->fe_drop_input = false;
#endif
}

#ifdef HACK_CHARDEV_FE_DROP_INPUT
static int fe_drop_char_can_read(void *opaque)
{
	(void)opaque;
	return INT_MAX;
}
static void fe_drop_chr_read(void *opaque, const uint8_t *buf, int size)
{
	(void)opaque;
	(void)buf;
	(void)size;
}
#endif /* HACK_CHARDEV_FE_DROP_INPUT */
#endif /* HACK_CHARDEV_SYNC */

void qemu_chr_fe_set_handlers(CharBackend *b,
                              IOCanReadHandler *fd_can_read,
                              IOReadHandler *fd_read,
                              IOEventHandler *fd_event,
                              BackendChangeHandler *be_change,
                              void *opaque,
                              GMainContext *context,
                              bool set_open)
{
    Chardev *s;
    int fe_open;

    s = b->chr;
    if (!s) {
        return;
    }

    if (!opaque && !fd_can_read && !fd_read && !fd_event) {
        fe_open = 0;
        remove_fd_in_watch(s);
    } else {
        fe_open = 1;
    }
#ifdef HACK_CHARDEV_FE_DROP_INPUT //TODO: refactor and make conditional
    if (CHARDEV_IS_MUX(s) && b->fe_drop_input) {
    	if (fd_can_read)
    		b->chr_can_read = fe_drop_char_can_read;
    	else
    		b->chr_can_read = NULL;
    	if (fd_read)
    		b->chr_read = fe_drop_chr_read;
    	else
    		b->chr_read = NULL;
    } else {
    	b->chr_can_read = fd_can_read;
    	b->chr_read = fd_read;
    }
#else
    b->chr_can_read = fd_can_read;
    b->chr_read = fd_read;
#endif /* HACK_CHARDEV_FE_DROP_INPUT */
    b->chr_event = fd_event;
    b->chr_be_change = be_change;
    b->opaque = opaque;

    qemu_chr_be_update_read_handlers(s, context);

    if (set_open) {
        qemu_chr_fe_set_open(b, fe_open);
    }

    if (fe_open) {
        qemu_chr_fe_take_focus(b);
        /* We're connecting to an already opened device, so let's make sure we
           also get the open event */
        if (s->be_open) {
            qemu_chr_be_event(s, CHR_EVENT_OPENED);
        }
#ifdef HACK_CHARDEV_SYNC //TODO: refactor and make conditional
        else if (CHARDEV_IS_MUX(s)) {
        	b->fe_drop_openclose = true;
        	qemu_bh_schedule(b->fe_deffered_open_bh);
        }
#endif
    }
#ifdef HACK_CHARDEV_SYNC //TODO: refactor and make conditional
    else if (CHARDEV_IS_MUX(s)) {
    	b->fe_drop_openclose = false;
    	qemu_bh_cancel(b->fe_deffered_open_bh);
    }
#endif

    if (CHARDEV_IS_MUX(s)) {
        mux_chr_set_handlers(s, context);
    }
}

void qemu_chr_fe_take_focus(CharBackend *b)
{
    if (!b->chr) {
        return;
    }

    if (CHARDEV_IS_MUX(b->chr)) {
        mux_set_focus(b->chr, b->tag);
    }
}

int qemu_chr_fe_wait_connected(CharBackend *be, Error **errp)
{
    if (!be->chr) {
        error_setg(errp, "missing associated backend");
        return -1;
    }

    return qemu_chr_wait_connected(be->chr, errp);
}

void qemu_chr_fe_set_echo(CharBackend *be, bool echo)
{
    Chardev *chr = be->chr;

    if (chr && CHARDEV_GET_CLASS(chr)->chr_set_echo) {
        CHARDEV_GET_CLASS(chr)->chr_set_echo(chr, echo);
    }
}

void qemu_chr_fe_set_open(CharBackend *be, int fe_open)
{
    Chardev *chr = be->chr;

    if (!chr) {
        return;
    }

    if (be->fe_open == fe_open) {
        return;
    }
    be->fe_open = fe_open;
    if (CHARDEV_GET_CLASS(chr)->chr_set_fe_open) {
        CHARDEV_GET_CLASS(chr)->chr_set_fe_open(chr, fe_open);
    }
}

guint qemu_chr_fe_add_watch(CharBackend *be, GIOCondition cond,
                            GIOFunc func, void *user_data)
{
#ifdef HACK_CHARDEV_FE_DROP_INPUT //TODO: refactor and make conditional
	(void)be;
	(void)cond;
	(void)func;
	(void)user_data;
	return 0;
#else
    Chardev *s = be->chr;
    GSource *src;
    guint tag;

    if (!s || CHARDEV_GET_CLASS(s)->chr_add_watch == NULL) {
        return 0;
    }

#ifdef HACK_CHARDEV_SYNC //TODO: make conditional
	cond &= ~(G_IO_OUT);
#endif

    src = CHARDEV_GET_CLASS(s)->chr_add_watch(s, cond);
    if (!src) {
        return 0;
    }

    g_source_set_callback(src, (GSourceFunc)func, user_data, NULL);
    tag = g_source_attach(src, s->gcontext);
    g_source_unref(src);

    return tag;
#endif /* HACK_CHARDEV_FE_DROP_INPUT */
}

void qemu_chr_fe_disconnect(CharBackend *be)
{
    Chardev *chr = be->chr;

    if (chr && CHARDEV_GET_CLASS(chr)->chr_disconnect) {
        CHARDEV_GET_CLASS(chr)->chr_disconnect(chr);
    }
}
