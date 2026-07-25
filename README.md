# Smart Schedule AI

Smart Schedule AI is a smart calendar project that uses artificial intelligence to make planning easier. Instead of manually adding every event, users can talk to the AI assistant and tell it what they want to schedule. The AI understands the request and creates events, reminders, and appointments automatically.

This project started because I struggle with staying organized and keeping on time, so I wanted to build something that could help solve that problem in a simple way.

I also made a hardware version of Smart Schedule that can show upcoming events without needing to open a phone. The goal was to create something useful, simple, and easy to access during the day so I can stay on top of everything.

## Disclaimer

The Groq API key shown in the code is not the real one, because I do not want other people using my API key. If you want to test the project, you can get your own free key from [console.groq.com/keys](https://console.groq.com/keys).

To use the app, you need to make an account in the **Sign Up** tab Or use the demo mode. After that, you can use the app normally.

#**Also make sure that you use the voice typing**

Video demo: [YouTube](https://youtu.be/1DnQlLGZJk8)
Website version: [smartschedule.wasmer.app](https://smartschedule.wasmer.app/)

## Features

### AI Scheduling Assistant

The main feature of Smart Schedule is the AI assistant. Instead of clicking through menus, users can just say what they want.

Examples:

* “Schedule a dentist appointment tomorrow at 3 PM.”
* “Remind me to study at 6 PM tonight.”

The AI takes the message and turns it into a calendar event.

### Smart Calendar

It works like a simple calendar app and includes:

* Daily, weekly, and monthly calendar views
* Creating and deleting events
* Reminders
* Cloud synchronization
* A simple interface

### Smart Schedule Device

I also built a physical device that connects to the Smart Schedule website and shows upcoming events, mostly because it was fun to make.

Features:

* Wi-Fi
* Cloud syncing
* Small design
* Shows the next event
* User-friendly interface

## Hardware Device

The Smart Schedule Device was one of the harder parts of the project because I had to make the hardware and software work together.

The device uses:

* ESP32-C3
* 0.96" SSD1306 OLED display
* Rechargeable LiPo battery
* Battery charging module
* Power switch

The device can:

* Connect to Wi-Fi
* Link to a Smart Schedule account
* Get events from the cloud
* Display the next event
* Run on battery power

## Problems I Had

While building Smart Schedule AI, I ran into a lot of problems.

One of the biggest problems was getting Firebase working correctly. At first, I had trouble understanding how the database worked and how to connect the ESP32 device to the cloud. I had to redo parts of the code multiple times before it finally worked. The JSON was also hard to handle because I could not find many good resources, but I fixed it.

Another problem was with the hardware. My first design for the device case did not fit all the parts correctly, so I had to redesign and 3D print it again. I also forgot to leave enough space for some components, which meant changing the design.

The AI part also had problems because I had to make sure the AI understood different ways people could ask for the same thing. For example, “make an event for tomorrow” and “remind me tomorrow” needed to be handled correctly, and the tool calls were hard to get working properly.

I also had to remove my API key from the public version because I did not want other people using it and wasting the limits.

## License

This project is licensed under the Creative Commons Attribution-NonCommercial 4.0 International License (CC BY-NC 4.0).

You can view, modify, and share this project for non-commercial use as long as credit is given.

Commercial use is not allowed without permission.

More information: [creativecommons.org/licenses/by-nc/4.0](https://creativecommons.org/licenses/by-nc/4.0/)

Created by Chahatesh.
