## Disclaimer

The Groq API key shown in the code is not the real one because I don't want people using my API key. If you want to test it, you can get your own free key from console.groq.com/keys

Also to use the app you need to make an acount in the sign up tab then you can use the app do not do sign in also here is a vid - https://youtu.be/1DnQlLGZJk8
The website version is working here
https://smartschedule.wasmer.app/

---
# Smart Schedule AI

Smart Schedule AI is a smart calendar project that uses artificial intelligence to make planning easier. Instead of having to manually add every event, users can talk with the AI assistant and tell it what they want to schedule. The AI understands what the user says and creates events, reminders, and appointments automatically.

This all started becouse I strugle with orgnising and also staying on time and I tought that this was a good way to fix this

I also made a hardware version of Smart Schedule that can show your upcoming events without needing to open your phone. The goal was to make something that is simple, useful, and easy to access during the day so I can do all of my stuff on time.


# Features

## AI Scheduling Assistant

The main feature of Smart Schedule is the AI assistant. Instead of clicking through menus, users can just say what they want.

Examples:

"Schedule a dentist appointment tomorrow at 3 PM."

"Remind me to study at 6 PM tonight."

The AI takes the message and turns it into a calendar event.

## Smart Calendar

Its just like google calender and it has 

* Daily, weekly, and monthly calendar views
* Creating and deleting events
* Reminders
* Cloud synchronization
* Simple interface

## Smart Schedule Device

I also built a physical device that connects to the Smart Schedule website and shows upcoming events becouse it was fun

Features:

* Wi-Fi
* Cloud syncing
* Small design
* Shows next event
* User frendly (I hope)

---

# Hardware Device

The Smart Schedule Device was one of the harder parts of the project because I had to make the hardware and software work together.

The device uses
* ESP32-C3
* 0.96" SSD1306 OLED Display
* Rechargeable LiPo battery
* Battery charging module
* Power switch

The device will
* Connect to Wi-Fi
* Link to a Smart Schedule account
* Get events from the cloud
* Display the next event
* Run on battery power

---

# Problems I Had

While building Smart Schedule AI, I ran into a lot of problems.

One of the biggest problems was getting Firebase working correctly. At first I had trouble understanding how the database worked and how to connect the ESP32 device to the cloud. I had to redo parts of the code multiple times before it finally worked. The json was hard becouse I could not find good resourses but I fixed it

Another problem was with the hardware. My first design for the device case did not fit all the parts correctly, so I had to redesign and 3D print it again. I also forgot to leave enough space for some components which caused me to change the design.

The AI part also had problems because I had to make sure the AI understood different ways people could ask for the same thing. For example, "make an event for tomorrow" and "remind me tomorrow" needed to be handled correctly and the tool calls were hard.

I also had to remove my API key from the public version because I didn't want other people using it and wasting the limits.

---

# How It Works

1. User signs into Smart Schedule AI.
2. User talks with the AI assistant.
3. The AI understands the request and creates an event.
4. The event is saved in Firebase.
5. Connected devices update and display the next event.

---

# Technology Used

## Frontend

* HTML
* CSS
* JavaScript

## Backend

* Firebase Authentication
* Firebase Firestore
* Firebase Hosting

## AI

* NLP
* Groq
* Tools

## Hardware

* ESP32-C3
* SSD1306 OLED Display
* LiPo Battery
* Charging Module
* Button

---


# License

This project is licensed under the Creative Commons Attribution-NonCommercial 4.0 International License (CC BY-NC 4.0).

You can view, modify, and share this project for non-commercial use as long as credit is given.

Commercial use is not allowed without permission.

More information:
https://creativecommons.org/licenses/by-nc/4.0/

---

Created by Chahatesh.
