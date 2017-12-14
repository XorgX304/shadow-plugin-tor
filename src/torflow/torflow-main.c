/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

#define TORFLOW_LOG_DOMAIN "torflow"

GLogLevelFlags torflowLogFilterLevel = G_LOG_LEVEL_INFO;

static const gchar* _torflowmain_logLevelToString(GLogLevelFlags logLevel) {
    switch (logLevel) {
        case G_LOG_LEVEL_ERROR:
            return "error";
        case G_LOG_LEVEL_CRITICAL:
            return "critical";
        case G_LOG_LEVEL_WARNING:
            return "warning";
        case G_LOG_LEVEL_MESSAGE:
            return "message";
        case G_LOG_LEVEL_INFO:
            return "info";
        case G_LOG_LEVEL_DEBUG:
            return "debug";
        default:
            return "default";
    }
}

void torflowmain_log(GLogLevelFlags level, const gchar* functionName, const gchar* format, ...) {
    if(level > torflowLogFilterLevel) {
        return;
    }

    va_list vargs;
    va_start(vargs, format);

    GDateTime* dt = g_date_time_new_now_local();
    GString* newformat = g_string_new(NULL);

    g_string_append_printf(newformat, "%04i-%02i-%02i %02i:%02i:%02i %"G_GINT64_FORMAT".%06i [%s] [%s] %s",
            g_date_time_get_year(dt), g_date_time_get_month(dt), g_date_time_get_day_of_month(dt),
            g_date_time_get_hour(dt), g_date_time_get_minute(dt), g_date_time_get_second(dt),
            g_date_time_to_unix(dt), g_date_time_get_microsecond(dt),
            _torflowmain_logLevelToString(level), functionName, format);

    gchar* message = g_strdup_vprintf(newformat->str, vargs);
    g_print("%s\n", message);
    g_free(message);

    g_string_free(newformat, TRUE);
    g_date_time_unref(dt);

    va_end(vargs);
}

int main(int argc, char *argv[]) {
    /* default log level, we can update this variable if we want it configurable */
    torflowLogFilterLevel = G_LOG_LEVEL_INFO;

    gchar hostname[128];
    memset(hostname, 0, 128);
    gethostname(hostname, 128);
	message("Starting torctl program on host %s process id %i", hostname, (gint)getpid());

	/* create the new state according to user inputs */
	TorFlowManager* torflow = torflowmanager_new(argc, argv);
	if(!torflow) {
		critical("Error initializing new TorCTL instance");
		return -1;
	}

	/* now we need to watch all of the descriptors in our main loop
	 * so we know when we can wait on any of them without blocking. */
	int mainepolld = epoll_create(1);
	if(mainepolld == -1) {
	    critical("Error in main epoll_create");
		close(mainepolld);
		return -1;
	}

	/* we have one main epoll descriptor that watches all of its sockets,
	 * so we now register that descriptor so we can watch for its events */
	struct epoll_event mainevent;
	mainevent.events = EPOLLIN|EPOLLOUT;
	mainevent.data.fd = torflowmanager_getEpollDescriptor(torflow);
	if(!mainevent.data.fd) {
	    critical("Error retrieving torflow epoll descriptor");
		close(mainepolld);
		return -1;
	}
	epoll_ctl(mainepolld, EPOLL_CTL_ADD, mainevent.data.fd, &mainevent);

	/* main loop - wait for events from the descriptors */
	struct epoll_event events[100];
	int nReadyFDs;
	message("entering main loop to watch descriptors");

	while(1) {
		/* wait for some events */
		debug("waiting for events");
		nReadyFDs = epoll_wait(mainepolld, events, 100, -1);
		if(nReadyFDs == -1) {
		    critical("Error in client epoll_wait");
			return -1;
		}

		/* activate if something is ready */
		debug("processing event");
		if(nReadyFDs > 0) {
		    torflowmanager_ready(torflow);
		}

		/* break out if done */
//		if(torctl_isDone(torctlState)) {
//			break;
//		}
	}

	message("finished main loop, cleaning up");

	/* de-register the epoll descriptor */
	mainevent.data.fd = torflowmanager_getEpollDescriptor(torflow);
	if(mainevent.data.fd) {
		epoll_ctl(mainepolld, EPOLL_CTL_DEL, mainevent.data.fd, &mainevent);
	}

	/* cleanup and close */
	close(mainepolld);
	torflowmanager_free(torflow);

	message("exiting cleanly");

	return 0;
}