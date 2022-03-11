# Yocto/Dgram: Ray-tracing diagram renderer

## Compilation
You can build the application using CMake with

```bin
mkdir build; cd build; cmake ..; cmake --build .
```

## Usage

To render o view a diagram you can use the `dgram` executable in `bin` with the commands `view` or `render`.

For diagrams that contain text labels you first need to render to textures said labels. Once rendered the first time they will be cached on disk.

To render the labes first run the text rendering server using the right `phantomjs` executable for your OS. 

You can find the executables for each OS in the respective folder inside the `text_server` folder. 

To run the server, execute `phantomjs` in a terminal inside the `text_server` folder, passing also as input the `server.js` file.

For example for Mac OS you need to run the command:

```bin
./mac/phantomjs server.js
```

Then you can render the labels using the `dgram` executable with the command `render_text`.