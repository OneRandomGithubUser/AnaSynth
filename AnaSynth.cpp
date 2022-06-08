#include <emscripten/val.h>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

#include <iostream>
#include <numbers>
#include <cmath>

emscripten::val window = emscripten::val::global("window");
emscripten::val document = emscripten::val::global("document");
const double pi = std::numbers::pi;
static int page;


extern "C"
{
EMSCRIPTEN_KEEPALIVE
void SelectPage(int i)
{
  page = i;
}
}

void InteractWithCanvas(emscripten::val event)
{
  std::string eventName = event["type"].as<std::string>();
  double pageX = event["pageX"].as<double>();
  double pageY = event["pageY"].as<double>();
  // std::cout << eventName << " " << pageX << " " << pageY << "\n";
}

void ResizeCanvas(emscripten::val event)
{
  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  emscripten::val ctx = canvas.call<emscripten::val>("getContext", emscripten::val("2d"));
  canvas.set("width", emscripten::val(window["innerWidth"].as<double>() * 0.7));
  canvas.set("height", emscripten::val(window["innerHeight"].as<double>() - 80));
  ctx.set("fillStyle", emscripten::val("white"));
}

// width: 60
void DrawResistor(emscripten::val ctx, int x, int y) {
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x, y);
  ctx.call<void>("lineTo", x+5, y-8);
  ctx.call<void>("lineTo", x+15, y+8);
  ctx.call<void>("lineTo", x+25, y-8);
  ctx.call<void>("lineTo", x+35, y+8);
  ctx.call<void>("lineTo", x+45, y-8);
  ctx.call<void>("lineTo", x+55, y+8);
  ctx.call<void>("lineTo", x+60, y);
  ctx.call<void>("stroke");
}

// width: 50
void DrawCapacitor(emscripten::val ctx, int x, int y) {
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x, y);
  ctx.call<void>("lineTo", x+20, y);
  ctx.call<void>("moveTo", x+20, y+20);
  ctx.call<void>("lineTo", x+20, y-20);
  ctx.call<void>("moveTo", x+30, y+20);
  ctx.call<void>("lineTo", x+30, y-20);
  ctx.call<void>("moveTo", x+30, y);
  ctx.call<void>("lineTo", x+50, y);
  ctx.call<void>("stroke");
}

// width: 60
void DrawInductor(emscripten::val ctx, int x, int y) {
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x, y);
  ctx.call<void>("arc", x+10, y, 10, pi, 0, false);
  ctx.call<void>("arc", x+30, y, 10, pi, 0, false);
  ctx.call<void>("arc", x+50, y, 10, pi, 0, false);
  ctx.call<void>("stroke");
}

void DrawSpeaker(emscripten::val ctx, int x, int y) {
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x, y-5);
  ctx.call<void>("lineTo", x, y+5);
  ctx.call<void>("lineTo", x+20, y+5);
  ctx.call<void>("lineTo", x+20, y-5);
  ctx.call<void>("lineTo", x, y-5);
  ctx.call<void>("lineTo", x, y-5);
  ctx.call<void>("lineTo", x-10, y-20);
  ctx.call<void>("lineTo", x+30, y-20);
  ctx.call<void>("lineTo", x+20, y-5);
  ctx.call<void>("stroke");
}

void DrawBattery(emscripten::val ctx, int x, int y) {
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x, y);
  ctx.call<void>("lineTo", x+10, y);
  ctx.call<void>("moveTo", x+10, y+10);
  ctx.call<void>("lineTo", x+10, y-10);
  ctx.call<void>("moveTo", x+20, y+20);
  ctx.call<void>("lineTo", x+20, y-20);
  ctx.call<void>("moveTo", x+30, y+10);
  ctx.call<void>("lineTo", x+30, y-10);
  ctx.call<void>("moveTo", x+40, y+20);
  ctx.call<void>("lineTo", x+40, y-20);
  ctx.call<void>("moveTo", x+40, y);
  ctx.call<void>("lineTo", x+50, y);
  ctx.call<void>("stroke");
}

void DrawFullCircuit(emscripten::val ctx) {
  DrawCapacitor(ctx, 170, 400);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 240, 380);
      ctx.call<void>("lineTo", 320, 380);
      ctx.call<void>("stroke");
      DrawInductor(ctx, 320, 380);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 380, 380);
      ctx.call<void>("lineTo", 430, 380);
      ctx.call<void>("lineTo", 430, 200);
      ctx.call<void>("lineTo", 200, 200);
      ctx.call<void>("stroke");
      DrawSpeaker(ctx, 180, 200);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 180, 200);
      ctx.call<void>("lineTo", 130, 200);
      ctx.call<void>("lineTo", 130, 400);
      ctx.call<void>("lineTo", 170, 400);
      ctx.call<void>("lineTo", 170, 320);
      ctx.call<void>("stroke");
      DrawBattery(ctx, 170, 320);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 220, 320);
      ctx.call<void>("lineTo", 220, 380);
      ctx.call<void>("lineTo", 234, 394);
      ctx.call<void>("stroke");
      ctx.set("fillStyle", emscripten::val("black"));
      ctx.call<void>("beginPath");
      ctx.call<void>("arc", 220, 400, 2, 0, 2*pi);
      ctx.call<void>("fill");
      ctx.call<void>("beginPath");
      ctx.call<void>("arc", 220, 380, 2, 0, 2*pi);
      ctx.call<void>("fill");
      ctx.call<void>("beginPath");
      ctx.call<void>("arc", 240, 380, 2, 0, 2*pi);
      ctx.call<void>("fill");
}

void RenderCanvas()
{
  static int FRAME_COUNT = 0;
  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  emscripten::val ctx = canvas.call<emscripten::val>("getContext", emscripten::val("2d"));
  ctx.set("fillStyle", emscripten::val("white"));
  ctx.call<void>("fillRect", 0, 0, canvas["width"], canvas["height"]);
  switch(page)
  {
    case 0:
      DrawCapacitor(ctx, 170, 400);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 220, 400);
      ctx.call<void>("lineTo", 320, 400);
      ctx.call<void>("stroke");
      DrawInductor(ctx, 320, 400);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 380, 400);
      ctx.call<void>("lineTo", 430, 400);
      ctx.call<void>("lineTo", 430, 200);
      ctx.call<void>("lineTo", 130, 200);
      ctx.call<void>("lineTo", 130, 400);
      ctx.call<void>("lineTo", 170, 400);
      ctx.call<void>("stroke");
      break;
    case 1:
      DrawFullCircuit(ctx);
      break;
    // case 2:
    //   std::cout << "b3" << std::endl;
    //   break;
    case 3:
      DrawFullCircuit(ctx);
      break;
    case 5:
      DrawFullCircuit(ctx);
      break;
    case 6:
      DrawFullCircuit(ctx);
      break;
    default:
      ctx.call<void>("fillRect", 0, 0, canvas["width"], canvas["height"]);
      ctx.call<void>("beginPath");
      ctx.call<void>("arc", 200 + 100*sin(FRAME_COUNT/(12*pi)), 150 + 75*sin(FRAME_COUNT/(7.5*pi)), abs(50*sin(FRAME_COUNT/(18*pi))), 0, 2 * pi);
      ctx.call<void>("stroke");
  }
  FRAME_COUNT++;
}

void Render()
{
  RenderCanvas();
}

int main() {
  document.call<void>("addEventListener",
                      emscripten::val("mousemove"),
                      emscripten::val::module_property("InteractWithCanvas"));

  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  emscripten::val ctx = canvas.call<emscripten::val>("getContext", emscripten::val("2d"));
  canvas.set("width", emscripten::val(window["innerWidth"].as<double>() * 0.7));
  canvas.set("height", emscripten::val(window["innerHeight"].as<double>() - 80));
  ctx.set("fillStyle", emscripten::val("white"));
  window.call<void>("addEventListener", emscripten::val("resize"), emscripten::val::module_property("ResizeCanvas"));

  // must be the last command in main()
  emscripten_set_main_loop(Render, 0, 1);
}


EMSCRIPTEN_BINDINGS(bindings)
{
  emscripten::function("InteractWithCanvas", InteractWithCanvas);
  emscripten::function("ResizeCanvas", ResizeCanvas);
  emscripten::function("SelectPage", SelectPage);
}
