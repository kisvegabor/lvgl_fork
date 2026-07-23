<p align="right">
  <b>English</b> | <a href="./docs/README_zh.md">中文</a> | <a href="./docs/README_pt_BR.md">Português do Brasil</a> | <a href="./docs/README_jp.md">日本語</a> | <a href="./docs/README_he.md">עברית</a>
</p>

<br>

<p align="center">
  <img src="https://lvgl.io/github-assets/logo-colored.png" width=300px>
</p>

<h1 align="center">Light and Versatile Graphics Library</h1>
<p align="center">
  <a href="..." title="...">📖 Docs</a>
  <a href="..." title="...">🤗 Forum</a>
  <a href="..." title="...">🪶 Blog</a>
  <a href="..." title="...">🤝 Services</a>
</p>

<br/>

<div align="center">
  <img src="2d demo gif">
  &nbsp;
  <img src="3d demo gif">
</div>

<br/>



### Table of Content

<p align="center">
  <a href="#..." title="...">Overview</a> •
  <a href="#..." title="...">LVGL Pro</a> •
  <a href="#..." title="...">Features</a> •
  <a href="#..." title="...">Examples</a> •
  <a href="#..." title="...">Integration</a> •
  <a href="#..." title="...">Contributing</a>
</p>

<br/>

## Overview


**LVGL** is a free and open-source UI library that enables you to create graphical user interfaces
for any MCUs and MPUs from any vendor on any platform.

**Requirements**: LVGL has no external dependencies, which makes it easy to compile for any modern target,
from small MCUs to multi-core Linux-based MPUs with 3D support. For a simple UI, you need only ~100kB RAM,
~200–300kB flash, and a buffer size of 1/10 of the screen for rendering.

**To get started**, pick a ready-to-use VSCode, Eclipse, or any other project and try out LVGL
on your PC. The LVGL UI code is fully platform-independent, so you can use the same UI code
on embedded targets too. Need help? Our [Docs](https://lvgl.io/docs/open) is AI ready! Just click <img src="docs/src/_static/images/ask_ai.png"/>
ask anything!

**LVGL is widely adopted** by chip vendors (like NXP, ESP-IDF, Renesas, and so on), RTOS projects (Zephyr, Nuttx, etc),
Board manufaturers (Riverdi, Seeed Studio, Viewer, Electrow, etc). If a developent board has display its very likely that vedor
offers LVGL support too.

### LVGL Pro

You can build any UIs with LVGL in C, however with [LVGL Pro](https://lvgl.io/pro) you can build UI in a much more professionals and maintainable way.
You can quickly create reusable components and screens, preview them instantly, and export normal LVGL C code that you can integrate anywhere.

LVGL Pro consists of four tightly related tools:

1. **Editor**: The heart of LVGL Pro. A desktop app to build components and screens in XML, manage data bindings, translations, animations, tests, and more. Learn more about the [XML Format](https://lvgl.io/docs/pro/syntax) and the [Widgets](https://lvgl.io/docs/pro/built_in_widgets).
2. **Online Viewer**: Run the Editor in your browser, open GitHub projects, and share easily without setting up a developer environment. Visit [https://viewer.lvgl.io](https://viewer.lvgl.io).
3. **Figma Plugin**: Move Fige design with one click to LVGL Pro. See how it works [here](https://lvgl.io/docs/pro/figma).
4. **CLI Tool**: Generate C code and run tests in CI/CD. See the details [here](https://lvgl.io/docs/pro/syntax/cli).

It comes with a **Free Community and Evaluation licences**, so you can jump [Download](https://lvgl.io/pro#download) it, and start using immediately.


## 💡 Features

**Free, Portable, and Scalable**
  - A fully portable C (C++ compatible) library with no external dependencies.
  - Can be compiled for any MCU or MPU, with any (RT)OS. Make, CMake, and simple globbing are all supported.
  - Supports monochrome, ePaper, OLED, or TFT displays, or even monitors. [Displays](https://docs.lvgl.io/master/main-modules/display/index.html)
  - Distributed under the MIT license, so you can easily use it in commercial projects too.
  - Needs only 32kB RAM and 128kB Flash, a frame buffer, and at least a 1/10 screen-sized buffer for rendering.
  - OS, external memory, and GPU are supported but not required.

**Widgets, Styles, Layouts, and More**
  - 30+ built-in [Widgets](https://docs.lvgl.io/master/widgets/index.html): Button, Label, Slider, Chart, Keyboard, Meter, Arc, Table, and many more.
  - Flexible [Style system](https://docs.lvgl.io/master/common-widget-features/styles/index.html) with ~100 style properties to customize any part of the widgets in any state.
  - [Flexbox](https://docs.lvgl.io/master/common-widget-features/layouts/flex.html) and [Grid](https://docs.lvgl.io/master/common-widget-features/layouts/grid.html)-like layout engines to automatically size and position the widgets responsively.
  - Text is rendered with UTF-8 encoding, supporting CJK, Thai, Hindi, Arabic, and Persian writing systems.
  - [Data bindings](https://docs.lvgl.io/master/main-modules/observer/index.html) to easily connect the UI with the application.
  - Rendering engine supports animations, anti-aliasing, opacity, smooth scrolling, shadows, image transformation, etc.
  - [Powerful 3D rendering engine](https://docs.lvgl.io/master/libs/gltf.html) to show [glTF models](https://sketchfab.com/) with OpenGL.
  - Supports Mouse, Touchpad, Keypad, Keyboard, External buttons, Encoder [Input devices](https://docs.lvgl.io/master/main-modules/indev.html).
  - [Multiple display](https://docs.lvgl.io/master/main-modules/display/overview.html#how-many-displays-can-lvgl-use) support.

## 🤖 Examples

You can check out more than 100 C and XML examples at https://lvgl.io/docs/open/examples

The [Online Viewer](https://viewer.lvgl.io/) of LVGL Pro also contains many Tutorials and the same Examples that you can play with interactively.

## 🤖 Examples

You can check out more than 100 examples at https://docs.lvgl.io/master/examples.html

The Online Viewer also contains tutorials to easily learn XML: https://viewer.lvgl.io/

As teaser here you can find a simple example too.
<img width="311" height="232" alt="image" src="https://github.com/user-attachments/assets/5948b485-e3f7-4a63-bb21-984381417c4a" />

<!-- show these side by side-->
<div>
```c
static void button_clicked_cb(lv_event_t * e)
{
	printf("Clicked\n");
}

[...]

  lv_obj_t * button = lv_button_create(lv_screen_active());
  lv_obj_center(button);
  lv_obj_add_event_cb(button, button_clicked_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * label = lv_label_create(button);
  lv_label_set_text(label, "Hello from LVGL!");
```

```xml
<screen>
	<view>
		<lv_button align="center">
			<event_cb callback="button_clicked_cb" />
			<lv_label text="Hello from LVGL!" />
		</lv_button>
	</view>
</screen>
```

</div>



## Integration

LVGL has no external dependencies, so it can be easily compiled for any devices. It comes with buuilt-in drviers, available in many package managers and RTOSes, but also easy port toany new devices.

### Pre integrated
<!-- Consider an other formtat with logos -->
Chip vendors: [ESP-IDF(ESP32) component](https://components.espressif.com/components/lvgl/lvgl), [NXP MCUXpresso component](https://www.nxp.com/design/software/embedded-software/lvgl-open-source-graphics-library:LITTLEVGL-OPEN-SOURCE-GRAPHICS-LIBRARY), Renesas FSP, ???

RTOS-es: [Zephyr library](https://docs.lvgl.io/master/integration/os/zephyr.html), [NuttX library](https://docs.lvgl.io/master/integration/os/nuttx.html), [RT-Thread RTOS](https://docs.lvgl.io/master/integration/os/rt-thread.html), ???

Frameworks: [Arduino library](https://docs.lvgl.io/master/integration/framework/arduino.html), [PlatformIO package](https://registry.platformio.org/libraries/lvgl/lvgl), CMSIS-Pack,  ???

Board manufaturers: Seeed, Electrow, Riverdi, ???

### Built-in driver
write something here based on https://lvgl.io/docs/open/integration
simulator, MCU, linux, GPU, etc drivers

### Porting manually

Integrating LVGL is very simple. Just drop it into any project and compile it as you would compile other files.
To configure LVGL, copy `lv_conf_template.h` as `lv_conf.h`, enable the first `#if 0`, and adjust the configs as needed.
(The default config is usually fine.) If available, LVGL can also be used with Kconfig.

Once in the project, you can initialize LVGL and create display and input devices as follows:

```c
#include "lvgl/lvgl.h" /*Define LV_LVGL_H_INCLUDE_SIMPLE to include as "lvgl.h"*/

#define TFT_HOR_RES 320
#define TFT_VER_RES 240

static uint32_t my_tick_cb(void)
{
    return my_get_millisec();
}

static void my_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    /*Write px_map to the area->x1, area->x2, area->y1, area->y2 area of the
     *frame buffer or external display controller. */

    /* signal LVGL that we're done */
    lv_display_flush_ready(disp);
}

static void my_touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
   if(my_touch_is_pressed()) {
       data->point.x = touchpad_x;
       data->point.y = touchpad_y;
       data->state = LV_INDEV_STATE_PRESSED;
   } else {
       data->state = LV_INDEV_STATE_RELEASED;
   }
}

void main(void)
{
    my_hardware_init();

    /*Initialize LVGL*/
    lv_init();

    /*Set millisecond-based tick source for LVGL so that it can track time.*/
    lv_tick_set_cb(my_tick_cb);

    /*Create a display where screens and widgets can be added*/
    lv_display_t * display = lv_display_create(TFT_HOR_RES, TFT_VER_RES);

    /*Add rendering buffers to the screen.
     *Here adding a smaller partial buffer assuming 16-bit (RGB565 color format)*/
    static uint8_t buf[TFT_HOR_RES * TFT_VER_RES / 10 * 2]; /* x2 because of 16-bit color depth */
    lv_display_set_buffers(display, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    /*Add a callback that can flush the content from `buf` when it has been rendered*/
    lv_display_set_flush_cb(display, my_flush_cb);

    /*Create an input device for touch handling*/
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touch_read_cb);

    /*The drivers are in place; now we can create the UI*/
    lv_obj_t * label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello world");
    lv_obj_center(label);

    /*Execute the LVGL-related tasks in a loop*/
    while(1) {
        lv_timer_handler();
        my_sleep_ms(5);         /*Wait a little to let the system breathe*/
    }
}
```


## Contributing

LVGL is an open project, and contributions are very welcome. There are many ways to contribute, from simply speaking about your project, writing examples, improving the documentation, fixing bugs, or even hosting your own project under the LVGL organization.

For a detailed description of contribution opportunities, visit the [Contributing](https://docs.lvgl.io/master/contributing/index.html)
section of the documentation.

Hundreds of people have already left their fingerprint on LVGL. Be one of them! See you here! 🙂

