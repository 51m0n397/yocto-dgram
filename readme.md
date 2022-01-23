# Yocto/Dgram: Ray-tracing diagram renderer

## Compilation
You can build the application using CMake with

```bin
mkdir build; cd build; cmake ..; cmake --build .
```

## Usage

First run the text rendering server using the right `phantomjs` executable for your OS. 

You can find the executables for each OS in the respective folder inside the `text_server` folder. 

To run the server, execute `phantomjs` in a terminal inside the `text_server` folder, passing also as input the `server.js` file.

For example for Mac OS you need to run the command:

```bin
./mac/phantomjs server.js
```

Once the server is running, you can run the `dgram` executable in `bin`.