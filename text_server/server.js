var port = 5500;
var server = require('webserver').create();
var service = server.listen(port, function (request, response) {

    console.log('Request at ' + new Date());
    console.log(JSON.stringify(request, null, 4));

    if (request.url == "/exit") {
        response.statusCode = 200;
        response.write('Exiting...');
        response.close();
        console.log('Exiting...');
        phantom.exit();
    }

    if (request.url == "/rasterize") {
        if (request.post.text == undefined) {
            response.statusCode = 400;
            response.write('text is missing');
            response.close();
        } else if (request.post.width == undefined) {
            response.statusCode = 400;
            response.write('width is missing');
            response.close();
        } else if (isNaN(request.post.width)) {
            response.statusCode = 400;
            response.write('width is not a number');
            response.close();
        } else if (request.post.height == undefined) {
            response.statusCode = 400;
            response.write('height is missing');
            response.close();
        } else if (isNaN(request.post.height)) {
            response.statusCode = 400;
            response.write('height is not a number');
            response.close();
        } else if (request.post.zoom == undefined) {
            response.statusCode = 400;
            response.write('zoom is missing');
            response.close();
        } else if (isNaN(request.post.zoom)) {
            response.statusCode = 400;
            response.write('zoom is not a number');
            response.close();
        } else {
            var page = require('webpage').create();
            var text = request.post.text;
            page.viewportSize = {
                width: parseInt(request.post.width, 10) * 2,
                height: parseInt(request.post.height, 10) * 2
            };
            page.zoomFactor = Number(request.post.zoom);

            var align_x = parseInt(request.post.align_x, 10);

            var align_x_css = "";
            if (align_x > 0)
                align_x_css = "text-align: right!important;";
            else if (align_x < 0)
                align_x_css = "text-align: left!important;";
            else if (align_x == 0)
                align_x_css = "text-align: center!important;";


            var color = "color: rgba(" + request.post.r + "," + request.post.g + "," + request.post.b + "," + request.post.a + ")";

            var style = "font-size:20pt; position: absolute; bottom: 0; width: 100%;" + align_x_css + color;

            page.onConsoleMessage = function (msg) {
                console.log(msg);
            };


            page.open("text.html", function () {
                page.evaluate(
                    function (text, width, style) {
                        var div = document.createElement("div");
                        div.innerHTML = format(text);
                        div.style.cssText = style;
                        document.body.appendChild(div);

                        renderMathInElement(document.body, {
                            delimiters: [
                                { left: '$$', right: '$$', display: true },
                                { left: '$', right: '$', display: false },
                                { left: '\\(', right: '\\)', display: false },
                                { left: '\\[', right: '\\]', display: true }
                            ],
                            throwOnError: false
                        });
                    },
                    text.indexOf('!!') > 0 ? text.slice(0, text.indexOf('!!')) : text,
                    page.viewportSize.width,
                    style
                );
                window.setTimeout(function () {
                    var image = page.renderBase64('PNG');
                    response.statusCode = 200;
                    response.write(image);
                    response.close();
                    page.close();
                }, 200);
            });
        }
    }
});

if (service) {
    console.log('Web server running on port ' + port);
} else {
    console.log('Error: Could not create web server listening on port ' + port);
    phantom.exit();
}