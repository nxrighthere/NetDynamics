/****************************** Module Header ******************************\
* Module Name:  netdynamics.c
* Project:      NetDynamics
* Copyright (c) 2019 Stanislav Denisov
*
* Data-oriented networking playground for the reliable UDP transports
*
* This source is subject to the Microsoft Public License.
* See https://opensource.org/licenses/MS-PL
* All other rights reserved.
*
* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
* EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
\***************************************************************************/

#include <math.h>
#include "aws/common/clock.h" // https://github.com/awslabs/aws-c-common
#include "aws/common/thread.h"
#include "jemalloc/jemalloc.h" // https://github.com/jemalloc/jemalloc
#include "raylib/raylib.h" // https://github.com/raysan5/raylib
#include "enet/enet.h" // https://github.com/nxrighthere/ENet-CSharp
#include "binn/binn.h" // https://github.com/liteserver/binn
#include "ini/ini.h" // https://github.com/benhoyt/inih

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 8

#define NET_TRANSPORT_HYPERNET 0
#define NET_TRANSPORT_ENET 1

#define NET_MAX_CLIENTS 32
#define NET_MAX_CHANNELS 2
#define NET_MAX_ENTITIES 100000
#define NET_MAX_ENTITY_SPAWN 10
#define NET_MAX_ENTITY_SPEED 80.0f

#define NET_MESSAGE_SPAWN 0xA
#define NET_MESSAGE_MOVE 0xB
#define NET_MESSAGE_DESTROY 0xC

typedef struct _Settings {
	uint8_t headlessMode;
	uint16_t resolutionWidth;
	uint16_t resolutionHeight;
	uint8_t framerateLimit;
	uint8_t vsync;
	uint8_t transport;
	char* ip;
	uint16_t port;
	uint8_t sendRate;
	uint32_t redundantBytes;
} Settings;

static uint8_t redundancyBuffer[1024 * 1024];

static const Color colors[] = {
	{ 250, 250, 250, 255 },
	{ 255, 0, 90, 255 },
	{ 94, 8, 255, 255 },
	{ 0, 80, 255, 255 },
	{ 0, 220, 255, 255 },
	{ 255, 255, 14, 255 }
};

static Settings settings;

static Font font;
static const int fontSize = 25;
static const int textureWidth = 32;
static const int textureHeight = 32;

static const char* status;
static const char* error;

#ifdef NETDYNAMICS_CLIENT
	static bool connected;
	static float worstLag;
#elif NETDYNAMICS_SERVER
	static uint32_t connected;
#endif

// ENet

static ENetHost* enetHost;
static ENetPeer* enetPeer;

// Strings

static const char* string_listening = "Listening for connections";
static const char* string_connecting = "Connecting to server...";
static const char* string_connected = "Connected to server";
static const char* string_disconnected = "Disconnected from server";
static const char* string_server_failed = "Server creation failed";
static const char* string_client_failed = "Client creation failed";
static const char* string_host_failed = "Host creation failed";
static const char* string_address_failed = "Address assignment failed";
static const char* string_listening_failed = "Server listening failed";
static const char* string_connection_failed = "Connection failed";

// Entities

typedef uint32_t Entity;

static Entity entity;

// Components

static Vector2* position;
static Vector2* speed;
static Color* color;
static Texture2D texture;
static Vector2* destination;

// Systems

#define ENTITIES_EXIST() color[0].a != 0

#ifdef NETDYNAMICS_SERVER
	inline static void entity_spawn(Vector2 positionComponent, uint32_t quantity) {
		uint32_t entities = entity + quantity;

		for (uint32_t i = entity; i < entities; i++) {
			if (entity < NET_MAX_ENTITIES) {
				entity++;
				position[i] = positionComponent;
				speed[i] = (Vector2){ (float)RayGetRandomValue(-300, 300) / 60.0f, (float)RayGetRandomValue(-300, 300) / 60.0f };
				color[i] = colors[RayGetRandomValue(0, sizeof(colors) / sizeof(Color) - 1)];
			}
		}
	}

	inline static void entity_move(float movementSpeed, float deltaTime) {
		#define TEXTURE_OFFSET 8

		for (uint32_t i = 0; i < entity; i++) {
			position[i].x += speed[i].x * movementSpeed * deltaTime;
			position[i].y += speed[i].y * movementSpeed * deltaTime;

			if (((position[i].x + textureWidth / 2 + TEXTURE_OFFSET) > settings.resolutionWidth) || ((position[i].x + textureWidth / 2 - TEXTURE_OFFSET) < 0))
				speed[i].x *= -1;

			if (((position[i].y + textureHeight / 2 + TEXTURE_OFFSET) > settings.resolutionHeight) || ((position[i].y + textureHeight / 2 - TEXTURE_OFFSET) < 0))
				speed[i].y *= -1;
		}
	}

	inline static void entity_destroy(Entity entityLocal) {
		entity = entityLocal;
		color[entityLocal].a = 0;
	}
#elif NETDYNAMICS_CLIENT
	inline static void entity_spawn(Entity entityRemote, Vector2 positionComponent, Vector2 speedComponent, Color colorComponent) {
		entity = entityRemote;
		position[entityRemote] = positionComponent;
		speed[entityRemote] = speedComponent;
		color[entityRemote] = colorComponent;
	}

	inline static void entity_move(Vector2* positionComponent, Vector2 destinationComponent, float maxDistanceDelta, float movementSpeed, float deltaTime) {
		float toVectorX = destinationComponent.x - positionComponent->x;
		float toVectorY = destinationComponent.y - positionComponent->y;
		float squareDistance = toVectorX * toVectorX + toVectorY * toVectorY;
		float step = maxDistanceDelta * movementSpeed * deltaTime;

		if (squareDistance == 0.0f || (step >= 0.0f && squareDistance <= step * step)) {
			*positionComponent = destinationComponent;

			return;
		}

		float distance = sqrtf(squareDistance);

		*positionComponent = (Vector2){ positionComponent->x + toVectorX / distance * step, positionComponent->y + toVectorY / distance * step };
	}

	inline static void entity_update(Entity entityRemote, Vector2 positionComponent, Vector2 speedComponent) {
		destination[entityRemote] = positionComponent;
		speed[entityRemote] = speedComponent;
	}

	inline static void entity_destroy(Entity entityRemote) {
		entity = entityRemote;
		color[entityRemote].a = 0;
	}

	inline static void entity_flush(void) {
		entity = 0;
		memset(position, 0, sizeof(Vector2));
		memset(speed, 0, sizeof(Vector2));
		memset(color, 0, sizeof(Color));
		memset(destination, 0, sizeof(Vector2));
	}
#endif

#ifdef NETDYNAMICS_SERVER
	inline static void message_send_to_all(uint8_t transport, uint8_t id, const Entity* entityLocal) {
		bool reliable = false;
		binn* data = binn_list();

		binn_list_add_uint8(data, id);

		if (id == NET_MESSAGE_SPAWN) {
			reliable = true;

			binn_list_add_uint32(data, *entityLocal);
			binn_list_add_float(data, position[*entityLocal].x);
			binn_list_add_float(data, position[*entityLocal].y);
			binn_list_add_float(data, speed[*entityLocal].x);
			binn_list_add_float(data, speed[*entityLocal].y);
			binn_list_add_uint8(data, color[*entityLocal].r);
			binn_list_add_uint8(data, color[*entityLocal].g);
			binn_list_add_uint8(data, color[*entityLocal].b);
		} else if (id == NET_MESSAGE_MOVE) {
			reliable = false;

			binn_list_add_uint32(data, *entityLocal);
			binn_list_add_float(data, position[*entityLocal].x);
			binn_list_add_float(data, position[*entityLocal].y);
			binn_list_add_float(data, speed[*entityLocal].x);
			binn_list_add_float(data, speed[*entityLocal].y);
		} else if (id == NET_MESSAGE_DESTROY) {
			reliable = true;

			binn_list_add_uint32(data, *entityLocal);
		} else {
			goto escape;
		}

		if (settings.redundantBytes > 0)
			binn_list_add_blob(data, redundancyBuffer, settings.redundantBytes);

		if (transport == NET_TRANSPORT_HYPERNET) {

		} else if (transport == NET_TRANSPORT_ENET) {
			ENetPacket* packet = enet_packet_create(binn_ptr(data), binn_size(data), !reliable ? ENET_PACKET_FLAG_NONE : ENET_PACKET_FLAG_RELIABLE);

			enet_host_broadcast(enetHost, 1, packet);
		}

		escape:

		binn_free(data);
	}
#endif

inline static void message_send(uint8_t transport, void* client, uint8_t id, const Entity* entityLocal) {
	bool reliable = false;
	binn* data = binn_list();

	binn_list_add_uint8(data, id);

	if (id == NET_MESSAGE_SPAWN) {
		reliable = true;

		#ifdef NETDYNAMICS_SERVER
			binn_list_add_uint32(data, *entityLocal);
			binn_list_add_float(data, position[*entityLocal].x);
			binn_list_add_float(data, position[*entityLocal].y);
			binn_list_add_float(data, speed[*entityLocal].x);
			binn_list_add_float(data, speed[*entityLocal].y);
			binn_list_add_uint8(data, color[*entityLocal].r);
			binn_list_add_uint8(data, color[*entityLocal].g);
			binn_list_add_uint8(data, color[*entityLocal].b);
		#elif NETDYNAMICS_CLIENT
			Vector2 mousePosition = RayGetMousePosition();

			binn_list_add_float(data, mousePosition.x);
			binn_list_add_float(data, mousePosition.y);
		#endif
	} else {
		goto escape;
	}

	if (settings.redundantBytes > 0)
		binn_list_add_blob(data, redundancyBuffer, settings.redundantBytes);

	if (transport == NET_TRANSPORT_HYPERNET) {

	} else if (transport == NET_TRANSPORT_ENET) {
		ENetPacket* packet = enet_packet_create(binn_ptr(data), binn_size(data), !reliable ? ENET_PACKET_FLAG_NONE : ENET_PACKET_FLAG_RELIABLE);

		enet_peer_send((ENetPeer*)client, 1, packet);
	}

	escape:

	binn_free(data);
}

inline static uint8_t message_receive(char* packet) {
	binn* data = binn_open(packet);
	uint8_t id = binn_list_uint8(data, 1);

	if (id == NET_MESSAGE_SPAWN) {
		#ifdef NETDYNAMICS_SERVER
			entity_spawn((Vector2){ binn_list_float(data, 2), binn_list_float(data, 3) }, NET_MAX_ENTITY_SPAWN);
		#elif NETDYNAMICS_CLIENT
			entity_spawn((Entity)binn_list_uint32(data, 2), (Vector2){ binn_list_float(data, 3), binn_list_float(data, 4) }, (Vector2){ binn_list_float(data, 5), binn_list_float(data, 6) }, (Color){ binn_list_uint8(data, 7), binn_list_uint8(data, 8), binn_list_uint8(data, 9), 255 });
		#endif
	} else if (id == NET_MESSAGE_MOVE) {
		#ifdef NETDYNAMICS_CLIENT
			static uint64_t currentLag;
			static uint64_t lastLag;

			if (aws_high_res_clock_get_ticks(&currentLag) == AWS_OP_SUCCESS) {
				if (lastLag > 0) {
					float lag = ((currentLag - lastLag) / 1000000.0f) / 1000.0f;

					if (lag > worstLag)
						worstLag = lag;
				}

				lastLag = currentLag;
			}

			entity_update((Entity)binn_list_uint32(data, 2), (Vector2){ binn_list_float(data, 3), binn_list_float(data, 4) }, (Vector2){ binn_list_float(data, 5), binn_list_float(data, 6) });
		#endif
	} else if (id == NET_MESSAGE_DESTROY) {
		#ifdef NETDYNAMICS_CLIENT
			entity_destroy((Entity)binn_list_uint32(data, 2));
		#endif
	}

	binn_free(data);

	return id;
}

inline static float get_frame_time(void) {
	if (!settings.headlessMode)
		return RayGetFrameTime();

	static uint64_t currentTime;
	static uint64_t lastTime;
	static float deltaTime;

	if (aws_high_res_clock_get_ticks(&currentTime) == AWS_OP_SUCCESS) {
		if (lastTime > 0)
			deltaTime = ((currentTime - lastTime) / 1000000.0f) / 1000.0f;

		lastTime = currentTime;
	}

	return deltaTime;
}

// Callbacks

static int ini_callback(void* data, const char* section, const char* name, const char* value) {
	#define FIELD_MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	#define PARSE_INTEGER(v) strtoul(v, NULL, 10)
	#define PARSE_STRING(v) strdup(v)

	Settings* settings = (Settings*)data;

	if (FIELD_MATCH("Display", "HeadlessMode"))
		settings->headlessMode = (uint8_t)PARSE_INTEGER(value);
	else if (FIELD_MATCH("Display", "ResolutionWidth"))
		settings->resolutionWidth = (uint16_t)PARSE_INTEGER(value);
	else if (FIELD_MATCH("Display", "ResolutionHeight"))
		settings->resolutionHeight = (uint16_t)PARSE_INTEGER(value);
	else if (FIELD_MATCH("Renderer", "FramerateLimit"))
		settings->framerateLimit = (uint8_t)PARSE_INTEGER(value);
	else if (FIELD_MATCH("Renderer", "VSync"))
		settings->vsync = (uint8_t)PARSE_INTEGER(value);
	else if (FIELD_MATCH("Network", "Transport"))
		settings->transport = (uint8_t)PARSE_INTEGER(value);
	else if (FIELD_MATCH("Network", "IP"))
		settings->ip = PARSE_STRING(value);
	else if (FIELD_MATCH("Network", "Port"))
		settings->port = (uint16_t)PARSE_INTEGER(value);
	else if (FIELD_MATCH("Network", "SendRate"))
		settings->sendRate = (uint8_t)PARSE_INTEGER(value);
	else if (FIELD_MATCH("Network", "RedundantBytes"))
		settings->redundantBytes = (uint32_t)PARSE_INTEGER(value);
	else
		return 0;

	return 1;
}

static uint64_t checksum_callback(const ENetBuffer* buffers, int bufferCount) {
	return enet_crc64(buffers, bufferCount);
}

int main(void) {
	// Settings

	if (ini_parse("settings.ini", ini_callback, &settings) < 0)
		abort();

	// Main

	char* title = NULL;

	#ifdef NETDYNAMICS_SERVER
		title = "NetDynamics (Server)";
	#elif NETDYNAMICS_CLIENT
		title = "NetDynamics (Client)";

		settings.headlessMode = 0;
	#endif

	if (settings.headlessMode) {
		static struct aws_allocator allocator;

		aws_common_library_init(&allocator);
	} else {
		if (settings.vsync > 0)
			RaySetConfigFlags(FLAG_VSYNC_HINT);

		RayInitWindow((int)settings.resolutionWidth, (int)settings.resolutionHeight, title);
		RaySetTargetFPS((int)settings.framerateLimit);

		font = RayLoadFontEx("share_tech.ttf", fontSize, 0, 0);

		RaySetTextureFilter(font.texture, FILTER_POINT);
	}

	// Serialization

	binn_set_alloc_functions(je_malloc, je_realloc, je_free);

	if (settings.redundantBytes > 0) {
		if (settings.redundantBytes > sizeof(redundancyBuffer))
			settings.redundantBytes = (uint32_t)sizeof(redundancyBuffer);

		for (uint32_t i = 0; i < settings.redundantBytes; i++) {
			redundancyBuffer[i] = i % sizeof(uint8_t);
		}
	}

	// Network

	char* name = NULL;

	if (settings.transport == NET_TRANSPORT_HYPERNET) {
		name = "HyperNet";


	} else if (settings.transport == NET_TRANSPORT_ENET) {
		name = "ENet";

		ENetCallbacks callbacks = {
			je_malloc,
			je_free,
			abort
		};

		if (enet_initialize_with_callbacks(enet_linked_version(), &callbacks) < 0) {
			error = "ENet initialization failed";
		} else {
			ENetAddress address = { 0 };

			address.port = settings.port;

			#ifdef NETDYNAMICS_SERVER
				if ((enetHost = enet_host_create(&address, NET_MAX_CLIENTS, NET_MAX_CHANNELS, 0, 0, 1024 * 1024)) == NULL)
					error = string_host_failed;
				else
					status = string_listening;
			#elif NETDYNAMICS_CLIENT
				if (enet_address_set_hostname(&address, settings.ip) < 0) {
					error = string_address_failed;
				} else {
					if ((enetHost = enet_host_create(NULL, 1, 0, 0, 0, 1024 * 1024)) == NULL) {
						error = string_host_failed;
					} else {
						if ((enetPeer = enet_host_connect(enetHost, &address, NET_MAX_CHANNELS, 0)) == NULL)
							error = string_connection_failed;
						else
							status = string_connecting;
					}
				}
			#endif

			if (enetHost != NULL) {
				enet_host_set_checksum_callback(enetHost, checksum_callback);
			}
		}
	} else {
		error = "Set the correct number of a network transport";
	}

	free(settings.ip);

	// Data

	if (error == NULL) {
		position = (Vector2*)je_calloc(NET_MAX_ENTITIES, sizeof(Vector2));
		speed = (Vector2*)je_calloc(NET_MAX_ENTITIES, sizeof(Vector2));
		color = (Color*)je_calloc(NET_MAX_ENTITIES, sizeof(Color));

		if (!settings.headlessMode)
			texture = RayLoadTexture("neon_circle.png");

		#ifdef NETDYNAMICS_CLIENT
			destination = (Vector2*)je_calloc(NET_MAX_ENTITIES, sizeof(Vector2));
		#endif
	}

	#ifdef NETDYNAMICS_SERVER
		float sendInterval = 1.0f / settings.sendRate;
	#elif NETDYNAMICS_CLIENT
		uint32_t rtt = 0;
	#endif

	while (settings.headlessMode || !RayWindowShouldClose()) {
		float deltaTime = get_frame_time();

		if (error == NULL) {
			// Transport
			if (settings.transport == NET_TRANSPORT_HYPERNET) {

			} else if (settings.transport == NET_TRANSPORT_ENET) {
				static ENetEvent event = { 0 };

				bool polled = false;

				while (!polled) {
					if (enet_host_check_events(enetHost, &event) <= 0) {
						if (enet_host_service(enetHost, &event, 0) <= 0)
							break;

						polled = true;
					}

					switch (event.type) {
						case ENET_EVENT_TYPE_NONE:
							break;

						case ENET_EVENT_TYPE_CONNECT: {
							#ifdef NETDYNAMICS_SERVER
								connected = enetHost->connectedPeers;

								if (ENTITIES_EXIST()) {
									for (uint32_t i = 0; i <= entity; i++) {
										message_send(NET_TRANSPORT_ENET, event.peer, NET_MESSAGE_SPAWN, &i);
									}
								}
							#elif NETDYNAMICS_CLIENT
								connected = true;
								status = string_connected;
							#endif

							break;
						}

						case ENET_EVENT_TYPE_DISCONNECT: case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT: {
							#ifdef NETDYNAMICS_SERVER
								connected = enetHost->connectedPeers;
							#elif NETDYNAMICS_CLIENT
								connected = false;
								worstLag = 0.0f;
								status = string_disconnected;

								entity_flush();
							#endif

							break;
						}

						case ENET_EVENT_TYPE_RECEIVE: {
							uint8_t id = message_receive((char*)event.packet->data);

							#ifdef NETDYNAMICS_SERVER
								if (id == NET_MESSAGE_SPAWN) {
									for (uint32_t i = entity - NET_MAX_ENTITY_SPAWN; i <= entity; i++) {
										message_send_to_all(NET_TRANSPORT_ENET, NET_MESSAGE_SPAWN, &i);
									}
								}
							#endif

							enet_packet_destroy(event.packet);

							break;
						}
					}
				}

				#ifdef NETDYNAMICS_CLIENT
					rtt = enetPeer->roundTripTime;
				#endif
			}

			// Timer
			#ifdef NETDYNAMICS_SERVER
				static float sendTime = 0.0f;

				sendTime += deltaTime;
			#endif

			// Spawn
			if (!settings.headlessMode) {
				if (RayIsMouseButtonDown(MOUSE_LEFT_BUTTON) || RayIsKeyPressed(KEY_SPACE)) {
					#ifdef NETDYNAMICS_SERVER
						entity_spawn(RayGetMousePosition(), NET_MAX_ENTITY_SPAWN);

						if (connected > 0) {
							if (settings.transport == NET_TRANSPORT_HYPERNET) {

							} else if (settings.transport == NET_TRANSPORT_ENET) {
								enet_host_flush(enetHost);

								for (uint32_t i = entity - NET_MAX_ENTITY_SPAWN; i <= entity; i++) {
									message_send_to_all(NET_TRANSPORT_ENET, NET_MESSAGE_SPAWN, &i);
								}
							}
						}
					#elif NETDYNAMICS_CLIENT
						if (connected) {
							if (settings.transport == NET_TRANSPORT_HYPERNET) {

							} else if (settings.transport == NET_TRANSPORT_ENET) {
								enet_host_flush(enetHost);

								message_send(NET_TRANSPORT_ENET, enetPeer, NET_MESSAGE_SPAWN, NULL);
							}
						}
					#endif
				}
			}

			// Move
			if (ENTITIES_EXIST()) {
				#ifdef NETDYNAMICS_SERVER
					entity_move(NET_MAX_ENTITY_SPEED, deltaTime);

					if (connected > 0) {
						if (sendTime >= sendInterval) {
							sendTime -= sendInterval;

							if (settings.transport == NET_TRANSPORT_HYPERNET) {

							} else if (settings.transport == NET_TRANSPORT_ENET) {
								enet_host_flush(enetHost);

								for (uint32_t i = 0; i < entity; i++) {
									message_send_to_all(NET_TRANSPORT_ENET, NET_MESSAGE_MOVE, &i);
								}
							}
						}
					}
				#elif NETDYNAMICS_CLIENT
					for (uint32_t i = 0; i < entity; i++) {
						if (destination[i].x == 0.0f && destination[i].y == 0.0f)
							continue;

						entity_move(&position[i], destination[i], sqrtf(speed[i].x * speed[i].x + speed[i].y * speed[i].y), NET_MAX_ENTITY_SPEED, deltaTime);
					}
				#endif
			}

			// Destroy
			#ifdef NETDYNAMICS_SERVER
				if (!settings.headlessMode) {
					if (ENTITIES_EXIST()) {
						if (RayIsMouseButtonDown(MOUSE_RIGHT_BUTTON) || RayIsKeyPressed(KEY_BACKSPACE)) {
							Entity entities = entity - NET_MAX_ENTITY_SPAWN;

							entity_destroy(entities);

							if (connected > 0) {
								if (settings.transport == NET_TRANSPORT_HYPERNET) {

								} else if (settings.transport == NET_TRANSPORT_ENET) {
									enet_host_flush(enetHost);

									message_send_to_all(NET_TRANSPORT_ENET, NET_MESSAGE_DESTROY, &entities);
								}
							}
						}
					}
				}
			#endif
		}

		if (!settings.headlessMode) {
			// Render
			RayBeginDrawing();
			RayClearBackground(CLITERAL{ 20, 0, 48, 255 });

			if (error != NULL) {
				// Error
				RayDrawTextEx(font, RayFormatText("ERROR %s", error), (Vector2){ 10, 10 }, fontSize, 0, WHITE);
			} else {
				// Entities
				if (ENTITIES_EXIST()) {
					for (uint32_t i = 0; i < entity; i++) {
						RayDrawTexture(texture, position[i].x, position[i].y, color[i]);
					}
				}

				// Stats
				static int fps = 0;
				static int counter = 0;
				static int refreshRate = 20;

				if (counter < refreshRate) {
					counter++;
				} else {
					fps = RayGetFPS();
					refreshRate = fps;
					counter = 0;
				}

				RayDrawTextEx(font, RayFormatText("FPS %i", fps), (Vector2){ 10, 10 }, fontSize, 0, WHITE);
				RayDrawTextEx(font, RayFormatText("ENTITIES %i", entity), (Vector2){ 10, 35 }, fontSize, 0, WHITE);
				RayDrawTextEx(font, name, (Vector2){ 10, 75 }, fontSize, 0, WHITE);
				RayDrawTextEx(font, RayFormatText("STATUS %s", status), (Vector2){ 10, 100 }, fontSize, 0, WHITE);

				#ifdef NETDYNAMICS_SERVER
					RayDrawTextEx(font, RayFormatText("CONNECTED CLIENTS %u/%u", connected, NET_MAX_CLIENTS), (Vector2){ 10, 125 }, fontSize, 0, WHITE);
					RayDrawTextEx(font, RayFormatText("SEND RATE %u", settings.sendRate), (Vector2){ 10, 150 }, fontSize, 0, WHITE);
					RayDrawTextEx(font, RayFormatText("MESSAGES PER SECOND %u", connected * entity * settings.sendRate), (Vector2){ 10, 175 }, fontSize, 0, WHITE);
				#elif NETDYNAMICS_CLIENT
					if (settings.transport == NET_TRANSPORT_HYPERNET) {

					} else if (settings.transport == NET_TRANSPORT_ENET) {
						RayDrawTextEx(font, RayFormatText("RTT %u", rtt), (Vector2){ 10, 125 }, fontSize, 0, WHITE);
						RayDrawTextEx(font, RayFormatText("Packets sent %u", enetPeer->totalPacketsSent), (Vector2){ 10, 150 }, fontSize, 0, WHITE);
						RayDrawTextEx(font, RayFormatText("Packets lost %u", enetPeer->totalPacketsLost), (Vector2){ 10, 175 }, fontSize, 0, WHITE);
						RayDrawTextEx(font, RayFormatText("Packets throttle %.1f%%", enet_peer_get_packets_throttle(enetPeer)), (Vector2){ 10, 200 }, fontSize, 0, WHITE);
						RayDrawTextEx(font, RayFormatText("Worst lag %.2f ms", worstLag), (Vector2){ 10, 225 }, fontSize, 0, WHITE);
					}
				#endif
			}

			RayEndDrawing();
		} else {
			aws_thread_current_sleep((1000 / settings.framerateLimit) * 1000000);
		}
	}

	if (settings.transport == NET_TRANSPORT_HYPERNET) {

	} else if (settings.transport == NET_TRANSPORT_ENET) {
		if (enetHost != NULL) {
			#ifdef NETDYNAMICS_SERVER
				for (uint32_t i = 0; i < enetHost->peerCount; i++) {
					enet_peer_disconnect_now(&enetHost->peers[i], 0);
				}
			#elif NETDYNAMICS_CLIENT
				if (enetPeer != NULL)
					enet_peer_disconnect_now(enetPeer, 0);
			#endif

			enet_host_flush(enetHost);
			enet_host_destroy(enetHost);
		}

		enet_deinitialize();
	}

	if (error == NULL) {
		je_free(position);
		je_free(speed);
		je_free(color);

		#ifdef NETDYNAMICS_CLIENT
			je_free(destination);
		#endif

		if (!settings.headlessMode)
			RayUnloadTexture(texture);
	}

	if (settings.headlessMode)
		aws_common_library_clean_up();
	else
		RayCloseWindow();
}
