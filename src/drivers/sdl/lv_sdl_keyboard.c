/**
 * @file lv_sdl_keyboard.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_sdl_keyboard.h"
#if LV_USE_SDL

#include "../../core/lv_group.h"
#include "../../stdlib/lv_string.h"
#include "lv_sdl_private.h"
#include "lc-gui/ui.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    char buf[KEYBOARD_BUFFER_SIZE];
    bool dummy_read;
} lv_sdl_keyboard_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void sdl_keyboard_read(lv_indev_t * indev, lv_indev_data_t * data);
static uint32_t keycode_to_ctrl_key(SDL_Keycode sdl_key);
static void release_indev_cb(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_indev_t * lv_sdl_keyboard_create(void)
{
    lv_sdl_keyboard_t * dsc = lv_malloc_zeroed(sizeof(lv_sdl_keyboard_t));
    LV_ASSERT_MALLOC(dsc);
    if(dsc == NULL) return NULL;

    lv_indev_t * indev = lv_indev_create();
    LV_ASSERT_MALLOC(indev);
    if(indev == NULL) {
        lv_free(dsc);
        return NULL;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, sdl_keyboard_read);
    lv_indev_set_driver_data(indev, dsc);
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    lv_indev_add_event_cb(indev, release_indev_cb, LV_EVENT_DELETE, indev);

    return indev;
}

static uint32_t last_key;
uint32_t ui_key_get(void)
{
    uint32_t key = last_key;
    last_key = 0;

    static int cnt = 0;
    if(key) cnt++;

    switch(key) {
        case LV_KEY_LEFT:
            key = UI_KEY_LEFT;
            break;
        case LV_KEY_RIGHT:
            key = UI_KEY_RIGHT;
            break;
        case LV_KEY_UP:
            key = UI_KEY_UP;
            break;
        case LV_KEY_DOWN:
            key = UI_KEY_DOWN;
            break;
        case LV_KEY_ENTER:
            key = UI_KEY_ENTER;
            break;
        case LV_KEY_ESC:
            key = UI_KEY_ESC;
            break;
        case 'f':
            return UI_KEY_FN;
        case 'p':
            ui_scr_cc_open_photo();
            return 0;
        case 'g':
            ui_scr_play_gallery_open();
            return 0;
        case 'l':
            ui_scr_play_open();
            return 0;
        case 'v':
            //            ui_scr_cc_open_photo();
            ui_scr_play_video_open();
            return 0;
        case 'm':
            if(cnt & 1) ui_scr_menu_open(NULL);
            else ui_scr_cc_open_photo();
            return 0;
        case 'c':
            ui_scr_capture_open();
            return 0;
        default:
            break;
    }


    return key;
}
/**********************
 *   STATIC FUNCTIONS
 **********************/

static void sdl_keyboard_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    lv_sdl_keyboard_t * dev = lv_indev_get_driver_data(indev);

    const size_t len = lv_strlen(dev->buf);

    /*Send a release manually*/
    if(dev->dummy_read) {
        dev->dummy_read = false;
        data->state = LV_INDEV_STATE_RELEASED;
    }
    /*Send the pressed character*/
    else if(len > 0) {
        dev->dummy_read = true;
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = dev->buf[0];
        lv_memmove(dev->buf, dev->buf + 1, len);
    }
}


static void release_indev_cb(lv_event_t * e)
{
    lv_indev_t * indev = (lv_indev_t *) lv_event_get_user_data(e);
    lv_sdl_keyboard_t * dev = lv_indev_get_driver_data(indev);
    if(dev) {
        lv_indev_set_driver_data(indev, NULL);
        lv_indev_set_read_cb(indev, NULL);
        lv_free(dev);
        LV_LOG_INFO("done");
    }
}

void lv_sdl_keyboard_handler(SDL_Event * event)
{
    uint32_t win_id = UINT32_MAX;
    switch(event->type) {
        case SDL_KEYDOWN:
            win_id = event->key.windowID;
            break;
        case SDL_TEXTINPUT:
            win_id = event->text.windowID;
            break;
        default:
            return;
    }

    lv_display_t * disp = lv_sdl_get_disp_from_win_id(win_id);


    /*Find a suitable indev*/
    lv_indev_t * indev = lv_indev_get_next(NULL);
    while(indev) {
        if(lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD && lv_indev_get_driver_data(indev)) {
            /*If disp is NULL for any reason use the first indev with the correct type*/
            if(disp == NULL || lv_indev_get_display(indev) == disp) break;
        }
        indev = lv_indev_get_next(indev);
    }
    if(indev == NULL) return;
    lv_sdl_keyboard_t * dsc = lv_indev_get_driver_data(indev);

    /* We only care about SDL_KEYDOWN and SDL_TEXTINPUT events */
    switch(event->type) {
        case SDL_KEYDOWN: {                     /*Button press*/
                const uint32_t ctrl_key = keycode_to_ctrl_key(event->key.keysym.sym);
                if(ctrl_key == '\0')
                    return;
                last_key = ctrl_key;
                const size_t len = lv_strlen(dsc->buf);
                if(len < KEYBOARD_BUFFER_SIZE - 1) {
                    dsc->buf[len] = ctrl_key;
                    dsc->buf[len + 1] = '\0';
                }
                break;
            }
        case SDL_TEXTINPUT: {                   /*Text input*/
                const size_t len = lv_strlen(dsc->buf) + lv_strlen(event->text.text);
                if(len < KEYBOARD_BUFFER_SIZE - 1)
                    strcat(dsc->buf, event->text.text);
            }
            last_key = event->text.text[0];
            break;
        default:
            break;

    }

    size_t len = lv_strlen(dsc->buf);
    while(len) {
        lv_indev_read(indev);

        /*Call again to handle dummy read in `sdl_keyboard_read`*/
        lv_indev_read(indev);
        len--;
    }
}

/**
 * Convert a SDL key code to it's LV_KEY_* counterpart or return '\0' if it's not a control character.
 * @param sdl_key the key code
 * @return LV_KEY_* control character or '\0'
 */
static uint32_t keycode_to_ctrl_key(SDL_Keycode sdl_key)
{
    /*Remap some key to LV_KEY_... to manage groups*/
    switch(sdl_key) {
        case SDLK_RIGHT:
        case SDLK_KP_PLUS:
            return LV_KEY_RIGHT;

        case SDLK_LEFT:
        case SDLK_KP_MINUS:
            return LV_KEY_LEFT;

        case SDLK_UP:
            return LV_KEY_UP;

        case SDLK_DOWN:
            return LV_KEY_DOWN;

        case SDLK_ESCAPE:
            return LV_KEY_ESC;

        case SDLK_BACKSPACE:
            return LV_KEY_BACKSPACE;

        case SDLK_DELETE:
            return LV_KEY_DEL;

        case SDLK_KP_ENTER:
        case '\r':
            return LV_KEY_ENTER;

        case SDLK_TAB:
        case SDLK_PAGEDOWN:
            return LV_KEY_NEXT;

        case SDLK_PAGEUP:
            return LV_KEY_PREV;

        case SDLK_HOME:
            return LV_KEY_HOME;

        case SDLK_END:
            return LV_KEY_END;

        default:
            return '\0';
    }
}

#endif /*LV_USE_SDL*/
