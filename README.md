# Datalogger using ArduinoJSON and SPIFFS

## Experimental!

This project is a "proof-of-concept" of a general purpose data-logger using esp8266

* Currently, it allows, browsing files on the esp-8266
* Viewing Contents of CSV Files in the webbrowser
* Download Files from browser
* editing html files using http://edatalogger/edit

## Description

* Data is written to SPIFFS at a certain interval in CSV format
* If filesize of the CSV is greater than set amount, it automatically creates a new file
* Contents are also "pushed" to Firebase


# WARNING

As there is a lot going on in the background, esp crashes a lot!!

Hopefully, it can be optimized, so that esp *knows* do one thing at a time and continue when another activity like firebase update stops

## To do

1. View contents of the display CSV File in a graph
- I wrote the code but seems forgot to SAVE!!

2. View Live data using webSockets
- Not sure if live data could be achieved without crashing

Any improvement of code is greatly appreciated so that it won't crash.

## Libraries:
- ESPAsyncWebServer by me-no-dev https://github.com/me-no-dev/ESPAsyncWebServer
- Arduino Time https://github.com/PaulStoffregen/Time
- Firebase Arduino Extended https://github.com/FirebaseExtended/firebase-arduino
