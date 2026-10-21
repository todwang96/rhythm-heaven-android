/* Touch input for Silksong menus. Check the method slots and class slots of each main build before use. */
#include "dm.h"
#include "niten_game.h"
#include "ss_pointer.h"
#include "ss_touch_ui.h"
#include <math.h>
#include <unistd.h>

void ss_input_set(uint64_t buttons, int32_t x, int32_t y);

/* Offsets in the main module. A method slot holds a relocated method pointer of the
   IL2CPP code registration. A class slot holds an Il2CppClass pointer after metadata setup. */
typedef struct { uintptr_t slot, method; } ss_touch_ui_method;
typedef struct {
    const char *build;
    ss_touch_ui_method process;                 /* HollowKnightInputModule.Process */
    ss_touch_ui_method mouse_down, mouse_up, mouse_held, mouse_position, mouse_scroll;   /* BaseInput */
    ss_touch_ui_method cursor_changed;          /* UIManager.<Start>b__137_0, the cursor visibility handler */
    ss_touch_ui_method raycast;                 /* GraphicRaycaster.Raycast */
    ss_touch_ui_method slider_down, slider_drag;   /* Slider.OnPointerDown, Slider.OnDrag */
    ss_touch_ui_method pane_left, pane_right;   /* ListenForPaneLeft.OnUpdate, ListenForPaneRight.OnUpdate */
    ss_touch_ui_method next_pane;               /* SetNextInventoryPane.OnEnter */
    uintptr_t process_mouse;                   /* StandaloneInputModule.ProcessMouseEvent(int) */
    uintptr_t screen_width, screen_height;
    uintptr_t object_name;                      /* UnityEngine.Object.get_name, for SS_TOUCH_TRACE */
    uintptr_t last_pointer;                     /* PointerInputModule.GetLastPointerEventData, for SS_TOUCH_TRACE */
    /* The cached native function of Cursor.lockState. The check address loads the cache in
       PointerInputModule.GetMousePointerEventData. */
    uintptr_t lock_cache, lock_name, resolve_native, lock_check;
    uint32_t lock_check_code;
    uintptr_t ui_class, game_class, player_class;
    /* The inventory. The class slots hold InventoryPaneList, InventoryItemManager, and GameCameras. */
    uintptr_t pane_list_class, manager_class, cameras_class;
    uintptr_t type_from_handle;                 /* System.Type.GetTypeFromHandle */
    uintptr_t child_component, child_components;   /* Component.GetComponent(s)InChildren(Type, bool) */
    uintptr_t component_transform, transform_position, world_to_screen, active_and_enabled;
    uintptr_t select_item;                      /* InventoryItemManager.SetSelected(selectable, direction, justDisplay) */
    uintptr_t in_sub_menu;                      /* InventoryPaneList.get_InSubMenu */
    uintptr_t selectable_down;                  /* Selectable.OnPointerDown */
    uintptr_t press_camera;                     /* PointerEventData.get_pressEventCamera */
    uintptr_t screen_to_local, rect_of;         /* RectTransformUtility.ScreenPointToLocalPointInRectangle, RectTransform.get_rect */
    uintptr_t fsm_event;                        /* Fsm.Event(FsmEventTarget, FsmEvent) */
} ss_touch_ui_layout;

static const ss_touch_ui_layout layouts[] = {
    {"fc9ea4cc (v655360, 1.0.30000)", {0x7dc95c8, 0x2a73ec0},
     {0x801dc80, 0x64925b0}, {0x801dc88, 0x6492620}, {0x801dc90, 0x6492690}, {0x801dc98, 0x6492700}, {0x801dca0, 0x6492770},
     {0x7da8828, 0x24ebe50}, {0x801af90, 0x6197890}, {0x801ca78, 0x64763d0}, {0x801ca80, 0x6476660},
     {0x7dcfa38, 0x2c92010}, {0x7dcfa58, 0x2c921f0}, {0x7ddfb50, 0x2f59720},
     0x6499830, 0x5f6bdf0, 0x5f6be50, 0x5ff4860, 0x6495a30,
     0x807dbc8, 0x654e50b, 0xa63bfc, 0x6495638, 0xf945e6a0, 0x7e195b8, 0x7e14880, 0x7e176b0,
     0x7e160a0, 0x7e16050, 0x7e14870, 0x529a320, 0x5fe0080, 0x5fe0560,
     0x5fdeef0, 0x6002e30, 0x5f54e50, 0x5fde250, 0x27b21f0, 0x27dccb0,
     0x6468490, 0x648a930, 0x6437700, 0x60003b0, 0x54fd140},
};

/* Il2CppClass fields and managed object fields of this IL2CPP version. */
enum { CLASS_STATICS = 0xb8, CLASS_READY = 0xe4, OBJECT_NATIVE = 0x10 };
enum { UI_INSTANCE = 0x10, UI_STATE = 0x64, UI_MENU = 0x68 };
enum { GAME_INSTANCE = 0x8, GAME_STATE = 0x20, GAME_IS_PAUSED = 0x24 };
enum { PLAYER_INSTANCE = 0x8, PLAYER_INVENTORY_OPEN = 0x8ac };
enum { MODULE_EVENT_SYSTEM = 0x38, MODULE_ALLOW_MOUSE = 0xd0, MODULE_HOVER_SELECT = 0xd1, EVENT_SYSTEM_SELECTED = 0x40 };
enum { STRING_LENGTH = 0x10, STRING_CHARS = 0x14 };
enum { POINTER_ENTER = 0x20, POINTER_PRESS = 0x28, POINTER_RAYCAST = 0x50 };   /* PointerEventData */
enum { LIST_ITEMS = 0x10, LIST_SIZE = 0x18, LIST_VERSION = 0x1c, RESULT_SIZE = 0x50 };   /* List<RaycastResult> */
enum { SLIDER_DIRECTION = 0x110, SLIDER_FILL_AREA = 0x140, SLIDER_HANDLE_AREA = 0x150 };   /* Slider */
/* UIManager.UIState and GameManager.GameState values. */
enum { UI_MAIN_MENU = 1, UI_PAUSED = 5, UI_OPTIONS = 6 };
enum { GAME_MAIN_MENU = 1, GAME_PLAYING = 4, GAME_PAUSED = 5 };
/* Managed arrays, Il2CppClass fields, and inventory fields. */
enum { ARRAY_LENGTH = 0x18, ARRAY_ITEMS = 0x20 };
enum { CLASS_NAME = 0x10, CLASS_TYPE = 0x20, CLASS_PARENT = 0x58 };
enum { PANE_LIST_INSTANCE = 0x0, PANE_LIST_PANES = 0x38, PANE_LIST_DISPLAY = 0x48, PANE_ACTIVE = 0x68 };
enum { DISPLAY_LEFT_ARROW = 0x30, DISPLAY_RIGHT_ARROW = 0x38, DISPLAY_ITEMS = 0x60, ITEM_PANE = 0x60 };
enum { MANAGER_SELECTED = 0x88, MANAGER_BLOCKED = 0x98, MAP_ZOOMED = 0x1d8, CAMERAS_INSTANCE = 0x0, CAMERAS_HUD = 0x20 };
/* PlayMaker fields, and the fields of ListenForPaneLeft/Right and SetNextInventoryPane. */
enum { ACTION_OWNER = 0x20, ACTION_FSM = 0x30, FSM_EVENT_TARGET = 0x120, FSM_INT_VALUE = 0x38 };
enum { LISTEN_PRESSED = 0x58, LISTEN_GAME = 0x78, NEXT_DIRECTION = 0x58, NEXT_CURRENT_INDEX = 0x68 };
/* Switch button bits of ss_input. */
enum { BUTTON_A = 0, BUTTON_L = 6, BUTTON_R = 7 };
enum { INVENTORY_ITEMS = 160, PANE_ICONS = 8 };
/* Sizes in 1920 x 1080 view units. A tap selects the nearest item in this distance. */
#define ITEM_REACH 110.0f
#define ITEM_TARGET 120.0f
#define ARROW_TARGET 120.0f
#define ICON_REACH 55.0f
/* A pane icon request ends when no pane listener takes it in this time. */
enum { PANE_REQUEST_FRAMES = 30 };
/* A press this far outside the slider line, as a part of the line length, still sets a value. */
#define SLIDER_LINE_MARGIN 0.15f
/* Keep the touch mode during gaps in the selection, for example a menu fade (about 55 frames).
   The controller comes back after this time. */
enum { MENU_GRACE_FRAMES = 90 };

typedef struct { float x, y; } vector2;
typedef struct { float x, y, z; } vector3;
typedef struct { float x, y, width, height; } ui_rect;
typedef struct { int ui, menu, game, inventory; } ss_touch_ui_state;

static const module_t *main_module;
static const ss_touch_ui_layout *layout;
static void (*process)(void *, const void *);
static void (*process_mouse)(void *, int, const void *);
static int (*screen_width)(const void *), (*screen_height)(const void *);
static void *(*object_name)(void *, const void *);
static void *(*last_pointer)(void *, int, const void *);
static void *(*resolve_native)(const char *);
static int (*lock_state)(void);
static int touch_call;
static int trace;
static int (*mouse_down)(void *, int, const void *), (*mouse_up)(void *, int, const void *), (*mouse_held)(void *, int, const void *);
static vector2 (*mouse_position)(void *, const void *), (*mouse_scroll)(void *, const void *);
static void (*cursor_changed)(void *, int, const void *);
static void (*raycast)(void *, void *, void *, const void *);
static void (*slider_down)(void *, void *, const void *), (*slider_drag)(void *, void *, const void *);
static void (*selectable_down)(void *, void *, const void *);
static void *(*press_camera)(void *, const void *);
static int (*screen_to_local)(void *, vector2, void *, vector2 *, const void *);
static ui_rect (*rect_of)(void *, const void *);
static void *(*type_from_handle)(const void *, const void *);
static void *(*child_component)(void *, void *, int, const void *);
static void *(*child_components)(void *, void *, int, const void *);
static void *(*component_transform)(void *, const void *);
static vector3 (*transform_position)(void *, const void *);
static vector3 (*world_to_screen)(void *, vector3, const void *);
static int (*active_and_enabled)(void *, const void *);
static int (*select_item)(void *, void *, uint64_t, int, const void *);
static int (*in_sub_menu)(void *, const void *);
static void (*pane_left)(void *, const void *), (*pane_right)(void *, const void *), (*next_pane)(void *, const void *);
static void (*fsm_event)(void *, void *, void *, const void *);
/* The input module frame. Each Unity frame has one. */
static unsigned frame_number;

/* The touch mouse. Only the game thread uses it, in the input module. */
static struct {
    int active, held, pressed, released;
    vector2 position;
} mouse;
static const vector2 outside = {-10000, -10000};

static void *read_pointer(const void *p, unsigned offset) { return p ? *(void *const *)((const uint8_t *)p + offset) : NULL; }
static int read_int(const void *p, unsigned offset) { return *(const int *)((const uint8_t *)p + offset); }
static int alive(const void *object) { return object && read_pointer(object, OBJECT_NATIVE); }
static int array_length(const void *array) { return array ? (int)*(const uint64_t *)((const uint8_t *)array + ARRAY_LENGTH) : 0; }
static void *array_item(const void *array, int i) { return read_pointer(array, ARRAY_ITEMS + i * sizeof(void *)); }

/* A class slot holds an encoded token until IL2CPP resolves it. */
static void *static_object(uintptr_t class_slot, unsigned offset) {
    uintptr_t klass = *(const uintptr_t *)(main_module->base + class_slot);
    if (!klass || (klass & 7) || !read_int((void *)klass, CLASS_READY)) return NULL;
    void *statics = read_pointer((void *)klass, CLASS_STATICS);
    void *object = statics ? read_pointer(statics, offset) : NULL;
    return alive(object) ? object : NULL;
}

static ss_touch_ui_state read_state(void) {
    ss_touch_ui_state state = {-1, -1, -1, -1};
    void *ui = static_object(layout->ui_class, UI_INSTANCE);
    if (ui) { state.ui = read_int(ui, UI_STATE); state.menu = read_int(ui, UI_MENU); }
    void *game = static_object(layout->game_class, GAME_INSTANCE);
    if (game) state.game = read_int(game, GAME_STATE);
    void *player = static_object(layout->player_class, PLAYER_INSTANCE);
    if (player) state.inventory = *((const uint8_t *)player + PLAYER_INVENTORY_OPEN);
    return state;
}

/* The title screen and the pause screen use uGUI menus. */
static int menu_state(ss_touch_ui_state state) {
    int title = state.game == GAME_MAIN_MENU && (state.ui == UI_MAIN_MENU || state.ui == UI_OPTIONS);
    int pause = state.game == GAME_PAUSED && (state.ui == UI_PAUSED || state.ui == UI_OPTIONS);
    return title || pause;
}

/* A menu always has a selected object. Without one, the screen can need a button. */
static int menu_selected(const void *module) {
    return alive(read_pointer(read_pointer(module, MODULE_EVENT_SYSTEM), EVENT_SYSTEM_SELECTED));
}

/* View coordinates use 1920 x 1080 for the full game picture. Unity puts the origin
   at the lower-left pixel of the screen. */
static vector2 screen_point(ss_pointer_event event) {
    float width = (float)screen_width(NULL), height = (float)screen_height(NULL);
    return (vector2){event.x / 1920 * width, (1 - event.y / 1080) * height};
}

/* Take at most one press or release in each frame. A quick tap then gives uGUI one
   frame with the button held. Moves only change the position. */
static void read_touches(uint64_t token) {
    mouse.pressed = mouse.released = 0;
    if (!mouse.held) mouse.position = outside;
    ss_pointer_event event;
    while (ss_pointer_take(token, &event)) {
        if (event.type == SS_POINTER_MOVE) {
            if (mouse.held) mouse.position = screen_point(event);
        } else if (event.type == SS_POINTER_DOWN && !mouse.held) {
            mouse.position = screen_point(event);
            mouse.held = mouse.pressed = 1;
            return;
        } else if (event.type == SS_POINTER_UP && mouse.held) {
            mouse.position = screen_point(event);
            mouse.held = 0;
            mouse.released = 1;
            return;
        } else if (event.type == SS_POINTER_CANCEL && mouse.held) {
            mouse.position = outside;   /* a release away from the press does not click */
            mouse.held = 0;
            mouse.released = 1;
            return;
        }
    }
}

/* The game locks the cursor after controller input. uGUI then puts the mouse at (-1, -1).
   The touch mouse is not the cursor, so it reads an unlocked state. */
static int cursor_lock_state(void) {
    if (touch_call) return 0;
    if (!lock_state) lock_state = resolve_native((const char *)main_module->base + layout->lock_name);
    return lock_state ? lock_state() : 0;
}

/* UIManager disables the menu raycaster when the game hides the cursor, for example after
   controller input. Touch input needs the raycaster, so it stays on. Only pointer input uses it. */
static void ui_cursor_changed(void *ui, int visible, const void *method) {
    (void)visible;
    cursor_changed(ui, 1, method);
}

static int input_down(void *input, int button, const void *method) {
    return mouse.active ? button == 0 && mouse.pressed : mouse_down(input, button, method);
}
static int input_up(void *input, int button, const void *method) {
    return mouse.active ? button == 0 && mouse.released : mouse_up(input, button, method);
}
static int input_held(void *input, int button, const void *method) {
    return mouse.active ? button == 0 && mouse.held : mouse_held(input, button, method);
}
static vector2 input_position(void *input, const void *method) {
    return mouse.active ? mouse.position : mouse_position(input, method);
}
static vector2 input_scroll(void *input, const void *method) {
    return mouse.active ? (vector2){0, 0} : mouse_scroll(input, method);
}

static const char *name_of(void *object, char *name, int size) {
    snprintf(name, size, "none");
    void *text = alive(object) ? object_name(object, NULL) : NULL;
    if (!text) return name;
    int length = read_int(text, STRING_LENGTH), i = 0;
    const uint16_t *chars = (const uint16_t *)((const uint8_t *)text + STRING_CHARS);
    for (; i < length && i < size - 1; ++i) name[i] = chars[i] < 128 ? (char)chars[i] : '?';
    name[i] = 0;
    return name;
}

static void log_touch(void *module) {
    static void *logged;
    void *selected = read_pointer(read_pointer(module, MODULE_EVENT_SYSTEM), EVENT_SYSTEM_SELECTED);
    char a[64], b[64], c[64];
    if (mouse.pressed || mouse.released) {
        void *data = last_pointer(module, -1, NULL);
        if (data) LOG("TOUCH UI %s at %.0f,%.0f: hit %s, enter %s, press %s", mouse.pressed ? "press" : "release",
                      mouse.position.x, mouse.position.y, name_of(read_pointer(data, POINTER_RAYCAST), a, sizeof a),
                      name_of(read_pointer(data, POINTER_ENTER), b, sizeof b), name_of(read_pointer(data, POINTER_PRESS), c, sizeof c));
    }
    if (selected == logged) return;
    logged = selected;
    LOG("TOUCH UI selected %s", name_of(selected, a, sizeof a));
}

/* The hit area of a menu slider handle is about one row above the handle picture. A press
   on a slider line can then go to the slider of the next row. On a touch press, the handles
   do not take the press when another item is under the finger. The slider below the finger
   then takes the press from its line. */
static int handle(void *result) {
    char name[16];
    return !strcmp(name_of(read_pointer(result, 0), name, sizeof name), "Handle");
}

static void raycast_hook(void *raycaster, void *data, void *list, const void *method) {
    int first = list ? read_int(list, LIST_SIZE) : 0;
    raycast(raycaster, data, list, method);
    if (!touch_call || !mouse.pressed || !list) return;
    void *array = read_pointer(list, LIST_ITEMS);
    int size = read_int(list, LIST_SIZE), handles = 0, kept = first;
    if (!array || first < 0 || size <= first || size > array_length(array)) return;
    uint8_t *results = (uint8_t *)array + ARRAY_ITEMS;
    for (int i = first; i < size; ++i) handles += handle(results + i * RESULT_SIZE);
    if (!handles || handles == size - first) return;
    for (int i = first; i < size; ++i) {
        if (handle(results + i * RESULT_SIZE)) continue;
        if (kept != i) memmove(results + kept * RESULT_SIZE, results + i * RESULT_SIZE, RESULT_SIZE);
        ++kept;
    }
    memset(results + kept * RESULT_SIZE, 0, (size_t)(size - kept) * RESULT_SIZE);
    *(int *)((uint8_t *)list + LIST_SIZE) = kept;
    ++*(int *)((uint8_t *)list + LIST_VERSION);
    if (trace) LOG("TOUCH UI press ignores %d slider handles of %d hits", handles, size - first);
}

/* uGUI sets a slider value from a press anywhere on the slider row, for example on the row
   name. A touch press away from the slider line only selects the row, as on a plain menu
   item. A drag of this press does not change the value. */
static void *off_line_slider;

static int on_slider_line(void *slider, void *event) {
    void *area = read_pointer(slider, SLIDER_HANDLE_AREA);
    if (!area) area = read_pointer(slider, SLIDER_FILL_AREA);   /* the click area of Slider.UpdateDrag */
    vector2 local;
    /* The C# method returns a bool. Only the low byte is defined. */
    if (!alive(area) || !(screen_to_local(area, mouse.position, press_camera(event, NULL), &local, NULL) & 0xff)) return 1;
    ui_rect line = rect_of(area, NULL);
    int vertical = read_int(slider, SLIDER_DIRECTION) >= 2;   /* BottomToTop or TopToBottom */
    float start = vertical ? line.y : line.x, length = vertical ? line.height : line.width;
    float position = vertical ? local.y : local.x;
    return length <= 0 || fabsf(position - start - length / 2) <= length * (0.5f + SLIDER_LINE_MARGIN);
}

static void slider_down_hook(void *slider, void *event, const void *method) {
    if (touch_call && !on_slider_line(slider, event)) {
        off_line_slider = slider;
        if (trace) LOG("TOUCH UI press away from the slider line selects the row only");
        selectable_down(slider, event, NULL);
        return;
    }
    slider_down(slider, event, method);
}

static void slider_drag_hook(void *slider, void *event, const void *method) {
    if (touch_call && slider == off_line_slider) return;
    slider_drag(slider, event, method);
}

/* Send the touch mouse to uGUI once in this frame. On a press, the game's hover rule
   selects the item under the finger. A drag, for example on a slider, keeps the selection.
   A press on an empty area clears the selection. The menu item then selects itself again
   at the end of the frame (MenuSelectable). */
static void process_menu_touch(void *module) {
    uint8_t *hover = (uint8_t *)module + MODULE_HOVER_SELECT, hovering = *hover;
    *hover = mouse.pressed;
    if (mouse.pressed) off_line_slider = NULL;
    touch_call = 1;
    process_mouse(module, 0, NULL);
    touch_call = 0;
    *hover = hovering;
    if (trace) log_touch(module);
}

/* The inventory (the "-" screen) has its own selection and cursor. It does not use uGUI.
   A tap selects the nearest item. The selected item is an A button target, so a hold
   also works. The pane arrows are L and R button targets. A tap on a pane icon asks
   for that pane. */
static struct {
    int disabled, sub_menu, icon_count;
    void *manager_type, *item_type;
    void *pane, *manager;
    struct { vector2 point; void *pane; } icons[PANE_ICONS];
} inventory;
/* The pane listeners send the L or R event of the request. Then the next pane step goes to
   the requested pane. "sent" is the frame of the event, or 0 before the event. */
static struct {
    void *pane;
    int from, to;
    unsigned frame, sent;
} pane_request;

static const char *class_name(const void *object) {
    const void *klass = object ? read_pointer(object, 0) : NULL;
    return klass ? read_pointer(klass, CLASS_NAME) : "";
}

/* Return the class of the object or of a base class with this name. */
static void *base_class(const void *object, const char *name) {
    for (void *klass = object ? read_pointer(object, 0) : NULL; klass; klass = read_pointer(klass, CLASS_PARENT)) {
        const char *klass_name = read_pointer(klass, CLASS_NAME);
        if (klass_name && !strcmp(klass_name, name)) return klass;
    }
    return NULL;
}

static void *type_object(void *klass) { return klass ? type_from_handle((uint8_t *)klass + CLASS_TYPE, NULL) : NULL; }

/* Unity screen pixels to view units. Unity puts the origin at the lower left. */
static int view_point(void *camera, void *component, vector2 *point) {
    if (!alive(camera) || !alive(component)) return 0;
    void *transform = component_transform(component, NULL);
    if (!alive(transform)) return 0;
    vector3 screen = world_to_screen(camera, transform_position(transform, NULL), NULL);
    float width = (float)screen_width(NULL), height = (float)screen_height(NULL);
    if (!(width > 0 && height > 0) || !isfinite(screen.x) || !isfinite(screen.y) || screen.z < 0) return 0;
    *point = (vector2){screen.x / width * 1920, (1 - screen.y / height) * 1080};
    return point->x >= 0 && point->x < 1920 && point->y >= 0 && point->y < 1080;
}

static void *hud_camera(void) {
    void *cameras = static_object(layout->cameras_class, CAMERAS_INSTANCE);
    void *camera = cameras ? read_pointer(cameras, CAMERAS_HUD) : NULL;
    return alive(camera) ? camera : NULL;
}

static void *active_pane(void *pane_list) {
    void *panes = read_pointer(pane_list, PANE_LIST_PANES);
    for (int i = 0, n = array_length(panes); i < n; ++i) {
        void *pane = array_item(panes, i);
        if (alive(pane) && *((const uint8_t *)pane + PANE_ACTIVE)) return pane;
    }
    return NULL;
}

/* A pane can hold more than one item manager. The manager with a live selection is active. */
static void *pane_manager(void *pane) {
    if (pane == inventory.pane && alive(inventory.manager) && alive(read_pointer(inventory.manager, MANAGER_SELECTED)))
        return inventory.manager;
    inventory.pane = pane;
    inventory.manager = NULL;
    void *managers = child_components(pane, inventory.manager_type, 0, NULL);
    for (int i = 0, n = array_length(managers); i < n; ++i) {
        void *manager = array_item(managers, i);
        if (alive(manager) && active_and_enabled(manager, NULL) && alive(read_pointer(manager, MANAGER_SELECTED))) {
            inventory.manager = manager;
            break;
        }
    }
    if (trace) {
        char a[64];
        LOG("TOUCH UI inventory pane %s (%s): %d managers, active %s", name_of(pane, a, sizeof a), class_name(pane),
            array_length(managers), inventory.manager ? class_name(inventory.manager) : "none");
    }
    return inventory.manager;
}

static int inventory_setup(void) {
    if (inventory.disabled) return 0;
    if (inventory.manager_type) return 1;
    uintptr_t slot = *(const uintptr_t *)(main_module->base + layout->manager_class);
    if (!slot || (slot & 7) || !read_int((void *)slot, CLASS_READY)) return 0;
    const char *name = read_pointer((void *)slot, CLASS_NAME);
    if (!name || strcmp(name, "InventoryItemManager")) {
        LOG("TOUCH UI inventory disabled: the manager class slot holds %s", name ? name : "null");
        inventory.disabled = 1;
        return 0;
    }
    inventory.manager_type = type_object((void *)slot);
    return inventory.manager_type != NULL;
}

static ss_pointer_rect square(vector2 center, float size) {
    return (ss_pointer_rect){center.x - size / 2, center.y - size / 2, size, size};
}

/* An arrow target ends halfway to the nearest pane icon. */
static ss_pointer_rect arrow_rect(vector2 arrow) {
    ss_pointer_rect rect = square(arrow, ARROW_TARGET);
    for (int i = 0; i < inventory.icon_count; ++i) {
        float icon = inventory.icons[i].point.x, middle = (arrow.x + icon) / 2;
        if (icon > arrow.x && middle < rect.x + rect.width) rect.width = middle - rect.x;
        if (icon < arrow.x && middle > rect.x) { rect.width -= middle - rect.x; rect.x = middle; }
    }
    return rect;
}

/* Fill the frame for the inventory. Return 0 when the inventory cannot take touches. */
static int inventory_frame(ss_pointer_frame *frame, void **manager_out, void **pane_out) {
    if (!inventory_setup()) return 0;
    void *pane_list = static_object(layout->pane_list_class, PANE_LIST_INSTANCE);
    void *camera = hud_camera();
    if (!pane_list || !camera) return 0;
    void *pane = active_pane(pane_list);
    void *manager = pane ? pane_manager(pane) : NULL;
    void *selected = manager ? read_pointer(manager, MANAGER_SELECTED) : NULL;
    if (!alive(selected)) return 0;
    inventory.sub_menu = in_sub_menu(pane_list, NULL);
    if (trace) {
        static void *logged;
        static int logged_sub = -1;
        if (selected != logged || inventory.sub_menu != logged_sub) {
            char a[64];
            LOG("TOUCH UI inventory selected %s (%s), sub-menu %d", name_of(selected, a, sizeof a), class_name(selected),
                inventory.sub_menu);
            logged = selected;
            logged_sub = inventory.sub_menu;
        }
    }
    /* The zoomed map moves with the stick. */
    if (!strcmp(class_name(manager), "InventoryMapManager") && *((const uint8_t *)manager + MAP_ZOOMED)) return 0;
    if (!inventory.item_type) inventory.item_type = type_object(base_class(selected, "InventoryItemSelectable"));
    if (!inventory.item_type) return 0;
    void *display = read_pointer(pane_list, PANE_LIST_DISPLAY);
    vector2 point;
    /* The display shows one icon for each available pane, in pane order. */
    void *icons = alive(display) ? read_pointer(display, DISPLAY_ITEMS) : NULL;
    void *icon_array = read_pointer(icons, LIST_ITEMS);
    int icon_total = icons ? read_int(icons, LIST_SIZE) : 0;
    if (icon_total > array_length(icon_array)) icon_total = array_length(icon_array);
    inventory.icon_count = 0;
    for (int i = 0; i < icon_total && inventory.icon_count < PANE_ICONS; ++i) {
        void *icon = array_item(icon_array, i), *icon_pane = read_pointer(icon, ITEM_PANE);
        if (alive(icon) && alive(icon_pane) && active_and_enabled(icon, NULL) && view_point(camera, icon, &point))
            inventory.icons[inventory.icon_count++] = (typeof(inventory.icons[0])){point, icon_pane};
    }
    const struct { unsigned offset; int button; } arrows[] = {{DISPLAY_LEFT_ARROW, BUTTON_L}, {DISPLAY_RIGHT_ARROW, BUTTON_R}};
    for (unsigned i = 0; alive(display) && i < sizeof arrows / sizeof arrows[0]; ++i)
        if (view_point(camera, read_pointer(display, arrows[i].offset), &point))
            frame->targets[frame->count++] = (ss_pointer_target){.id = 1 + i, .rect = arrow_rect(point),
                                                                .flags = SS_POINTER_BUTTON, .button = arrows[i].button};
    if (trace) {
        static int logged_icons = -1;
        if (inventory.icon_count != logged_icons) {
            char a[64];
            for (int i = 0; i < inventory.icon_count; ++i)
                LOG("TOUCH UI pane icon %d %s at %.0f,%.0f", i, name_of(inventory.icons[i].pane, a, sizeof a),
                    inventory.icons[i].point.x, inventory.icons[i].point.y);
            for (int i = 0; i < frame->count; ++i)
                LOG("TOUCH UI pane arrow %d from x %.0f to %.0f", i, frame->targets[i].rect.x,
                    frame->targets[i].rect.x + frame->targets[i].rect.width);
            logged_icons = inventory.icon_count;
        }
    }
    if (view_point(camera, selected, &point))
        frame->targets[frame->count++] = (ss_pointer_target){.id = 3, .rect = square(point, ITEM_TARGET),
                                                            .flags = SS_POINTER_BUTTON | SS_POINTER_SELECTED, .button = BUTTON_A};
    *manager_out = manager;
    *pane_out = pane;
    return 1;
}

/* Containers forward the selection to their children. They are not tap targets. */
static int container(const void *item) {
    const char *name = class_name(item);
    return !strcmp(name, "InventoryItemGrid") || !strcmp(name, "InventoryItemGroup");
}

/* In a sub-menu, for example the tool list of a crest slot, the game moves the selection
   only between items of the selected kind. A tap does the same. */
static void inventory_tap(void *manager, void *pane, vector2 tap) {
    void *selected = read_pointer(manager, MANAGER_SELECTED);
    if (*((const uint8_t *)manager + MANAGER_BLOCKED) || !alive(selected)) return;
    void *camera = hud_camera();
    void *items = camera ? child_components(pane, inventory.item_type, 0, NULL) : NULL;
    void *best = NULL;
    float best_distance = ITEM_REACH * ITEM_REACH;
    char a[64];
    for (int i = 0, n = array_length(items); i < n; ++i) {
        void *item = array_item(items, i);
        vector2 point;
        if (!alive(item) || container(item) || !active_and_enabled(item, NULL) || !view_point(camera, item, &point)) continue;
        if (inventory.sub_menu && read_pointer(item, 0) != read_pointer(selected, 0)) continue;
        float dx = point.x - tap.x, dy = point.y - tap.y, distance = dx * dx + dy * dy;
        if (trace) LOG("TOUCH UI inventory item %s (%s) at %.0f,%.0f", name_of(item, a, sizeof a), class_name(item), point.x, point.y);
        if (distance < best_distance) { best = item; best_distance = distance; }
    }
    if (trace) LOG("TOUCH UI inventory tap at %.0f,%.0f: %d items, nearest %s", tap.x, tap.y, array_length(items),
                   best ? name_of(best, a, sizeof a) : "none");
    if (best && best != selected) select_item(manager, best, 0, 0, NULL);
}

static int pane_index(void *panes, const void *pane) {
    for (int i = 0, n = array_length(panes); pane && i < n; ++i)
        if (array_item(panes, i) == pane) return i;
    return -1;
}

/* Return 1 when the tap hits a pane icon. The game moves to the pane through its own L or R
   event, so the pane animation and the pane state stay correct. */
static int pane_tap(vector2 tap) {
    int hit = -1;
    float best_distance = ICON_REACH * ICON_REACH;
    for (int i = 0; i < inventory.icon_count; ++i) {
        float dx = inventory.icons[i].point.x - tap.x, dy = inventory.icons[i].point.y - tap.y;
        if (dx * dx + dy * dy < best_distance) { hit = i; best_distance = dx * dx + dy * dy; }
    }
    if (hit < 0) return 0;
    void *pane_list = static_object(layout->pane_list_class, PANE_LIST_INSTANCE);
    void *panes = read_pointer(pane_list, PANE_LIST_PANES);
    int from = pane_index(panes, active_pane(pane_list)), to = pane_index(panes, inventory.icons[hit].pane);
    if (inventory.sub_menu || from < 0 || to < 0 || from == to) {
        if (trace) LOG("TOUCH UI pane icon %d tap: no move (pane %d to %d, sub-menu %d)", hit, from, to, inventory.sub_menu);
        return 1;
    }
    pane_request = (typeof(pane_request)){.pane = inventory.icons[hit].pane, .from = from, .to = to, .frame = frame_number};
    LOG("TOUCH UI pane icon %d tap: pane %d to %d", hit, from, to);
    return 1;
}

static void read_inventory_touches(uint64_t token, void *manager, void *pane) {
    ss_pointer_event event;
    while (ss_pointer_take(token, &event))
        if (event.type == SS_POINTER_DOWN && !pane_tap((vector2){event.x, event.y}))
            inventory_tap(manager, pane, (vector2){event.x, event.y});
}

/* A pane listener sends its "was pressed" event for a request in its direction, like a real
   L or R press. All listeners of the same frame take the request. */
static void pane_listen(void *action, int right, void (*original)(void *, const void *), const void *method) {
    void *game = read_pointer(action, LISTEN_GAME), *event = read_pointer(action, LISTEN_PRESSED);
    if (!pane_request.pane || (pane_request.to > pane_request.from) != right || !event ||
        (pane_request.sent && pane_request.sent != frame_number) || (game && *((const uint8_t *)game + GAME_IS_PAUSED))) {
        original(action, method);
        return;
    }
    void *fsm = read_pointer(action, ACTION_FSM);
    if (!fsm) { original(action, method); return; }
    pane_request.sent = frame_number;
    if (trace) {
        char a[64];
        LOG("TOUCH UI pane icon sends %s to %s", right ? "R" : "L", name_of(read_pointer(action, ACTION_OWNER), a, sizeof a));
    }
    fsm_event(fsm, read_pointer(fsm, FSM_EVENT_TARGET), event, NULL);
}

static void pane_left_hook(void *action, const void *method) { pane_listen(action, 0, pane_left, method); }
static void pane_right_hook(void *action, const void *method) { pane_listen(action, 1, pane_right, method); }

/* The pane step adds its direction to the current pane index. For a sent request, the step
   goes from the current pane to the requested pane in one move. */
static void next_pane_hook(void *action, const void *method) {
    void *direction = read_pointer(action, NEXT_DIRECTION), *current = read_pointer(action, NEXT_CURRENT_INDEX);
    if (!pane_request.pane || !pane_request.sent || !direction || !current) { next_pane(action, method); return; }
    void *pane_list = static_object(layout->pane_list_class, PANE_LIST_INSTANCE);
    int *step = (int *)((uint8_t *)direction + FSM_INT_VALUE), saved = *step;
    int from = read_int(current, FSM_INT_VALUE), to = pane_request.to;
    int valid = from == pane_request.from && (saved > 0) == (to > from) &&
                pane_index(read_pointer(pane_list, PANE_LIST_PANES), pane_request.pane) == to;
    pane_request.pane = NULL;
    if (valid) *step = to - from;
    next_pane(action, method);
    *step = saved;
    LOG("TOUCH UI pane step %d from pane %d%s, now pane %d", valid ? to - from : saved, from,
        valid ? "" : " (request does not match)", read_int(current, FSM_INT_VALUE));
}

static void process_hook(void *module, const void *method) {
    static ss_touch_ui_state logged = {-2, -2, -2, -2};
    static int grace, logged_kind = -1;
    static ss_pointer_frame frame;   /* too large for the game thread stack */
    ++frame_number;
    ss_touch_ui_state state = read_state();
    if (memcmp(&state, &logged, sizeof state)) {
        LOG("TOUCH UI state ui=%d menu=%d game=%d inventory=%d", state.ui, state.menu, state.game, state.inventory);
        logged = state;
    }
    if (!menu_state(state)) grace = 0;
    else if (menu_selected(module)) grace = MENU_GRACE_FRAMES;
    frame = (ss_pointer_frame){.owner = 1, .mode = SS_POINTER_NONE};
    void *manager = NULL, *pane = NULL;
    int ui_menu = grace > 0, kind = 0;
    if (ui_menu) {
        --grace;
        frame.mode = SS_POINTER_MENU;
        kind = 1;
    } else if (state.inventory == 1 && state.game == GAME_PLAYING && inventory_frame(&frame, &manager, &pane)) {
        frame.mode = SS_POINTER_MENU;
        kind = 2;
    } else {
        frame.count = 0;
    }
    if (kind != logged_kind) {
        static const char *const kinds[] = {"controller", "menu", "inventory"};
        LOG("TOUCH UI mode %s at frame %u", kinds[kind], frame_number);
        logged_kind = kind;
    }
    if (pane_request.pane && (state.inventory != 1 ||
                              frame_number - (pane_request.sent ? pane_request.sent : pane_request.frame) > PANE_REQUEST_FRAMES)) {
        LOG("TOUCH UI pane icon request to pane %d ends (%s)", pane_request.to,
            state.inventory != 1 ? "inventory closed" : pane_request.sent ? "no pane step" : "no listener");
        pane_request.pane = NULL;
    }
    uint64_t token = ss_pointer_publish(&frame);
    if (!ui_menu) {
        process(module, method);
        if (mouse.held) {
            /* The menu closed during a touch. Release the button away from the menu items. */
            mouse.position = outside;
            mouse.held = mouse.pressed = 0;
            mouse.released = 1;
            touch_call = 1;
            process_mouse(module, 0, NULL);
            touch_call = 0;
        }
        mouse = (typeof(mouse)){0};
        if (manager) read_inventory_touches(token, manager, pane);
        return;
    }
    if (!mouse.active) mouse = (typeof(mouse)){.active = 1, .position = outside};
    read_touches(token);
    /* The original method sends controller input. It also sends mouse input when the
       game shows a cursor. Here the touch mouse goes to uGUI exactly once. */
    uint8_t *allow = (uint8_t *)module + MODULE_ALLOW_MOUSE, allowed = *allow;
    *allow = 0;
    process(module, method);
    *allow = allowed;
    process_menu_touch(module);
}

static int slot_matches(const module_t *main, ss_touch_ui_method m) {
    return main->size > m.slot + sizeof(uintptr_t) && main->text_end > m.method &&
           *(uintptr_t *)(main->base + m.slot) == (uintptr_t)main->base + m.method;
}

static int layout_matches(const module_t *main, const ss_touch_ui_layout *candidate) {
    const ss_touch_ui_method methods[] = {candidate->process, candidate->mouse_down, candidate->mouse_up,
                                          candidate->mouse_held, candidate->mouse_position, candidate->mouse_scroll,
                                          candidate->cursor_changed, candidate->raycast, candidate->slider_down,
                                          candidate->slider_drag, candidate->pane_left, candidate->pane_right,
                                          candidate->next_pane};
    for (unsigned i = 0; i < sizeof methods / sizeof methods[0]; ++i)
        if (!slot_matches(main, methods[i])) return 0;
    const uintptr_t code[] = {candidate->process_mouse, candidate->screen_width, candidate->screen_height,
                              candidate->object_name, candidate->last_pointer, candidate->type_from_handle,
                              candidate->child_component, candidate->child_components, candidate->component_transform,
                              candidate->transform_position, candidate->world_to_screen, candidate->active_and_enabled,
                              candidate->select_item, candidate->in_sub_menu, candidate->selectable_down,
                              candidate->press_camera, candidate->screen_to_local, candidate->rect_of,
                              candidate->fsm_event};
    for (unsigned i = 0; i < sizeof code / sizeof code[0]; ++i)
        if (main->text_end <= code[i]) return 0;
    const uintptr_t classes[] = {candidate->ui_class, candidate->game_class, candidate->player_class,
                                 candidate->pane_list_class, candidate->manager_class, candidate->cameras_class};
    for (unsigned i = 0; i < sizeof classes / sizeof classes[0]; ++i)
        if (main->size <= classes[i] + sizeof(uintptr_t)) return 0;
    static const char lock_name[] = "UnityEngine.Cursor::get_lockState()";
    if (main->size <= candidate->lock_cache + sizeof(uintptr_t) || main->size <= candidate->lock_name + sizeof lock_name ||
        main->text_end <= candidate->lock_check + 4 || main->text_end <= candidate->resolve_native) return 0;
    return !memcmp(main->base + candidate->lock_name, lock_name, sizeof lock_name) &&
           *(const uint32_t *)(main->base + candidate->lock_check) == candidate->lock_check_code &&
           *(const uintptr_t *)(main->base + candidate->lock_cache) == 0;
}

static void *hook(ss_touch_ui_method m, void *replacement) {
    uintptr_t *slot = (void *)(main_module->base + m.slot);
    void *original = (void *)*slot;
    *slot = (uintptr_t)replacement;
    return original;
}

/* Development input for the Mac host. Each line is "sequence type x y" in 1920 x 1080 view
   coordinates. Type 5 takes a screenshot. Normal runs leave SS_TOUCH_FILE unset. */
static void *test_file(void *argument) {
    const char *path = argument, *shots = getenv("SS_TOUCH_SHOTS");
    unsigned long long previous = 0;
    uint64_t held_button = 0;
    for (;;) {
        FILE *file = fopen(path, "r");
        if (file) {
            unsigned long long sequence; int type; float x, y;
            if (fscanf(file, "%llu %d %f %f", &sequence, &type, &x, &y) == 4 && sequence != previous) {
                previous = sequence;
                ss_pointer_frame frame;
                if (type == 5 && shots) {
                    char output[1024]; snprintf(output, sizeof output, "%s/touch-%llu.png", shots, sequence);
                    dm_gfx_shot_at(1, output);
                    LOG("TOUCH UI screenshot %s", output);
                } else if (type >= SS_POINTER_DOWN && type <= SS_POINTER_CANCEL && ss_pointer_read(&frame)) {
                    /* Like App.m, a button target holds its button and does not send a pointer event. */
                    int target = type == SS_POINTER_DOWN ? ss_pointer_hit(&frame, x, y) : -1;
                    if (target >= 0 && (frame.targets[target].flags & SS_POINTER_BUTTON)) {
                        held_button = 1ull << frame.targets[target].button;
                        ss_input_set(held_button, 0, 0);
                    } else if (held_button && type != SS_POINTER_MOVE) {
                        held_button = 0;
                        ss_input_set(0, 0, 0);
                    } else if (!held_button) {
                        ss_pointer_push((ss_pointer_event){frame.token, type, x, y});
                    }
                }
                LOG("TOUCH UI test %llu type %d %.1f %.1f", sequence, type, x, y);
            }
            fclose(file);
        }
        usleep(5000);
    }
}

static void start_test_file(void) {
    const char *name = getenv("SS_TOUCH_FILE");
    if (!name || !*name) return;
    pthread_t thread;
    if (pthread_create(&thread, NULL, test_file, (void *)name)) die("Cannot start touch test input");
    pthread_detach(thread);
}

void ss_touch_ui_install(void) {
    for (int i = 0; i < g_nmod; ++i)
        if (!strcmp(g_mod[i].name, "main")) main_module = &g_mod[i];
    if (!main_module) die("Touch UI adapter cannot find the main module");
    for (unsigned i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i)
        if (layout_matches(main_module, &layouts[i])) layout = &layouts[i];
    if (!layout) { LOG("TOUCH UI disabled: main module does not match a supported build"); return; }
    uint8_t *base = main_module->base;
    process_mouse = (void *)(base + layout->process_mouse);
    screen_width = (void *)(base + layout->screen_width);
    screen_height = (void *)(base + layout->screen_height);
    object_name = (void *)(base + layout->object_name);
    last_pointer = (void *)(base + layout->last_pointer);
    resolve_native = (void *)(base + layout->resolve_native);
    type_from_handle = (void *)(base + layout->type_from_handle);
    child_component = (void *)(base + layout->child_component);
    child_components = (void *)(base + layout->child_components);
    component_transform = (void *)(base + layout->component_transform);
    transform_position = (void *)(base + layout->transform_position);
    world_to_screen = (void *)(base + layout->world_to_screen);
    active_and_enabled = (void *)(base + layout->active_and_enabled);
    select_item = (void *)(base + layout->select_item);
    in_sub_menu = (void *)(base + layout->in_sub_menu);
    selectable_down = (void *)(base + layout->selectable_down);
    press_camera = (void *)(base + layout->press_camera);
    screen_to_local = (void *)(base + layout->screen_to_local);
    rect_of = (void *)(base + layout->rect_of);
    fsm_event = (void *)(base + layout->fsm_event);
    *(uintptr_t *)(base + layout->lock_cache) = (uintptr_t)cursor_lock_state;
    trace = getenv("SS_TOUCH_TRACE") != NULL;
    mouse_down = hook(layout->mouse_down, input_down);
    mouse_up = hook(layout->mouse_up, input_up);
    mouse_held = hook(layout->mouse_held, input_held);
    mouse_position = hook(layout->mouse_position, input_position);
    mouse_scroll = hook(layout->mouse_scroll, input_scroll);
    cursor_changed = hook(layout->cursor_changed, ui_cursor_changed);
    raycast = hook(layout->raycast, raycast_hook);
    slider_down = hook(layout->slider_down, slider_down_hook);
    slider_drag = hook(layout->slider_drag, slider_drag_hook);
    pane_left = hook(layout->pane_left, pane_left_hook);
    pane_right = hook(layout->pane_right, pane_right_hook);
    next_pane = hook(layout->next_pane, next_pane_hook);
    process = hook(layout->process, process_hook);
    LOG("TOUCH UI main %s: input module hooks installed", layout->build);
    start_test_file();
}
