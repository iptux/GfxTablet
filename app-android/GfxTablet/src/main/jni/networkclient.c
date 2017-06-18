/*
 * networkclient.c
 * network client for GfxTablet
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <math.h>
#include <jni.h>

#include "protocol.h"

#define EVENT_TYPE_QUIT 0xff

struct event_compat {
	struct event_compat *next;
	uint8_t type;
	uint16_t x, y;
	uint16_t pressure;
	int8_t button;
	int8_t down;
};

struct network_client {
	int sock;
	struct sockaddr_in addr;
	struct event_packet packet;
	struct event_compat *head, *last;
	pthread_mutex_t mutex;
	sem_t sem;
	float maxx, maxy;
	int running;
};

struct event_compat *network_client_new_event(uint8_t type)
{
	struct event_compat *event = malloc(sizeof(struct event_compat));
	if (NULL != event) {
		memset(event, 0, sizeof(struct event_compat));
		event->type = type;
	}
	return event;
}

struct network_client *network_client_new()
{
	struct sockaddr_in addr;
	int n;
	struct network_client *client = NULL;
	client = malloc(sizeof(struct network_client));
	if (NULL == client) {
		return 0;
	}
	memset(client, 0, sizeof(struct network_client));

	client->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (client->sock < 0) {
		goto out;
	}

	n = 1;
	setsockopt(client->sock, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));

	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(client->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		goto out;
	}

	if (0 != pthread_mutex_init(&client->mutex, NULL)) {
		goto out;
	}

	if (0 != sem_init(&client->sem, 0, 0)) {
		pthread_mutex_destroy(&client->mutex);
		goto out;
	}

	memcpy(client->packet.signature, "GfxTablet", sizeof(client->packet.signature));
	client->packet.version = htons(PROTOCOL_VERSION);

	return client;

out:
	if (client->sock > 0) {
		close(client->sock);
	}
	if (client) {
		free(client);
	}
	return 0;
}

void network_client_free(struct network_client *client)
{
	if (NULL == client) {
		return;
	}

	client->running = 0;
	if (client->sock > 0) {
		close(client->sock);
	}
	sem_destroy(&client->sem);
	pthread_mutex_destroy(&client->mutex);
	free(client);
}

void network_client_put_event(struct network_client *client, struct event_compat *event)
{
	if (NULL == event) {
		return;
	}

	if (NULL == client) {
		free(event);
		return;
	}

	pthread_mutex_lock(&client->mutex);
	if (NULL == client->last) {
		client->head = event;
	} else {
		client->last->next = event;
	}
	client->last = event;
	pthread_mutex_unlock(&client->mutex);
	sem_post(&client->sem);
}

struct event_compat *network_client_get_event(struct network_client *client)
{
	struct event_compat *event = NULL;
	if (NULL == client) {
		return NULL;
	}

	sem_wait(&client->sem);
	pthread_mutex_lock(&client->mutex);
	if (NULL == client->head) {
		// bug
		goto out;
	}

	event = client->head;
	client->head = event->next;
	if (event == client->last) {
		client->last = NULL;
	}

out:
	pthread_mutex_unlock(&client->mutex);
	return event;
}

jlong Java_at_bitfire_gfxtablet_NetworkClient_init(JNIEnv* env, jobject thiz)
{
	return (jlong) network_client_new();
}

void Java_at_bitfire_gfxtablet_NetworkClient_close(JNIEnv* env, jobject thiz, jlong handle)
{
	struct network_client *client = (struct network_client *) handle;
	if (NULL == client) {
		return;
	}

	struct event_compat *event = network_client_new_event(EVENT_TYPE_QUIT);

	client->running = 0;
	network_client_put_event(client, event);
}

void Java_at_bitfire_gfxtablet_NetworkClient_setSize(JNIEnv* env, jobject thiz, jlong handle, jint x, jint y)
{
	struct network_client *client = (struct network_client *) handle;
	if (NULL == client) {
		return;
	}

	client->maxx = x;
	client->maxy = y;
}

jboolean Java_at_bitfire_gfxtablet_NetworkClient_setHost(JNIEnv* env, jobject thiz, jlong handle, jstring host, jint port)
{
	struct network_client *client = (struct network_client *) handle;
	if (NULL == client) {
		return JNI_FALSE;
	}

	const char *hostname = (*env)->GetStringUTFChars(env, host, 0);
	struct hostent *hp = gethostbyname(hostname);
	(*env)->ReleaseStringUTFChars(env, host, hostname);

	if (NULL == hp) {
		return JNI_FALSE;
	}

	client->addr.sin_family = hp->h_addrtype;
	client->addr.sin_port = htons(port);
	memcpy(&client->addr.sin_addr, hp->h_addr, hp->h_length);
	return JNI_TRUE;
}

u_int16_t normalize(float v, float max)
{
	double value = fminf(fmaxf(v, 0), max);
	value *= UINT16_MAX;
	value /= max;
	u_int16_t ret = value;
	return ret;
}

void Java_at_bitfire_gfxtablet_NetworkClient_putEvent(JNIEnv* env, jobject thiz, jlong handle, jbyte type, jfloat x, jfloat y, jfloat pressure, jint button, jboolean down)
{
	struct network_client *client = (struct network_client *) handle;
	if (NULL == client) {
		return;
	}

	struct event_compat *event = network_client_new_event(type);
	if (NULL == event) {
		return;
	}

	event->x = normalize(x, client->maxx);
	event->y = normalize(y, client->maxy);
	event->pressure = normalize(pressure, 2);
	event->button = button;
	event->down = down;
	network_client_put_event(client, event);
}

void Java_at_bitfire_gfxtablet_NetworkClient_loop(JNIEnv* env, jobject thiz, jlong handle)
{
	struct network_client *client = (struct network_client *) handle;
	if (NULL == client) {
		return;
	}

	size_t length;
	struct event_compat *event;
	client->running = 1;
	while (client->running) {
		event = network_client_get_event(client);
		if (NULL == event) {
			break;
		}
		if (EVENT_TYPE_QUIT == event->type) {
			free(event);
			break;
		}

		length = sizeof(client->packet);
		client->packet.type = event->type;
		client->packet.x = htons(event->x);
		client->packet.y = htons(event->y);
		client->packet.pressure = htons(event->pressure);
		if (EVENT_TYPE_BUTTON == event->type) {
			client->packet.button = event->button;
			client->packet.down = event->down;
		} else {
			length -= 2;
		}
		free(event);

		if (0 > sendto(client->sock, &client->packet, length, 0, (struct sockaddr *) &client->addr, sizeof(client->addr))) {
			break;
		}
	}

	network_client_free(client);
}
