<p align="center"> 
  <img src="https://i.imgur.com/kOArjco.png" alt="alt logo">
</p>

[![GitHub release](https://img.shields.io/github/release/nxrighthere/NetDynamics.svg)](https://github.com/nxrighthere/NetDynamics/releases) [![PayPal](https://drive.google.com/uc?id=1OQrtNBVJehNVxgPf6T6yX1wIysz1ElLR)](https://www.paypal.me/nxrighthere) [![Bountysource](https://drive.google.com/uc?id=19QRobscL8Ir2RL489IbVjcw3fULfWS_Q)](https://salt.bountysource.com/checkout/amount?team=nxrighthere) [![Coinbase](https://drive.google.com/uc?id=1LckuF-IAod6xmO9yF-jhTjq1m-4f7cgF)](https://commerce.coinbase.com/checkout/03e11816-b6fc-4e14-b974-29a1d0886697)

NetDynamics is a data-oriented networking playground for the reliable UDP transports. The application was created for stress testing and debugging a proprietary networking library [HyperNet](https://github.com/users/nxrighthere/projects/1), but it also supports [ENet](https://github.com/nxrighthere/ENet-CSharp) as an open-source alternative. You can see it in action [here](https://mega.nz/#!gc8TUQrQ!Ad18ZJCZtrRu6SJACMXJWm3izGEfgoiG4TdoGDso_io).

<p align="center">
  <img src="https://media.giphy.com/media/XEfdHjad7IYULdJM7g/giphy.gif">
</p>

Purpose
--------
NetDynamics allows to spawn up to 100,000 dynamic entities, efficiently process data, and render graphics using draw call batching. The application generates a huge amount of data for transfer over a network or on loopback using UDP transport that supports sequenced reliable/unreliable message delivery. The primary goal is to determine problematic spots, bottlenecks, or bugs in a network transport and visualize it in real-time.

How it works?
--------
The overall approach is based on the Entity Component System where an entity is just an identifier which decoupled from data and logic. NetDynamics is a client-server application which synchronizes visual representation of entities across connections. The server is serializing and transmitting to clients large batches of components that essentially are entity's data. The systems are used for logic and to process components for designated entities.

The server has full authority over all entities, clients can only participate in the population of a world by sending an appropriate message. The server can spawn entities as well, and also it can destroy them locally with further synchronization across clients. The server is sending state updates for entities at a fixed interval (20 updates per second by default). Clients are using interpolation to replicate the fluent movement of entities between state updates based on the position and speed components. Extrapolation is not implemented so packet loss will be noticeable.

The application is designed to generate traffic exponentially with hundreds of thousands of network messages. It's not multi-threaded intentionally to notice performance degradation of the main thread when a network transport is under high-load, thus a single-threaded transport will always perform with higher latencies depending on the application's framerate. Moving transport logic to a separate dedicated thread or making it framerate independent in any other way will solve this, but it's beyond the purpose of NetDynamics.

Usage
--------
[Download](https://github.com/nxrighthere/NetDynamics/releases) the application and set the desired parameters in the `settings.ini` file. Run the application, use the left mouse button on server or client to spawn entities, use the right mouse button on server to destroy entities.

For testing an initial application's rendering and processing performance to get a visual difference in consumption of a frame time by networking logic, you can simply spawn entities on server without any connections.
