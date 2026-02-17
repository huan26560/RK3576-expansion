#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

int mqtt_client_init(const char *host, int port);
int mqtt_publish(const char *topic, const char *payload);
void mqtt_client_loop(int timeout_ms);
void mqtt_client_cleanup(void);
// 检查MQTT是否连接
int mqtt_is_connected(void);
#endif
