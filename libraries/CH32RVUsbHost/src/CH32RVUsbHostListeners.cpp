/* CH32RVUsbHost::add*Listener() / removeListener() (CH32RVUsbHost.h),
 * EspUsbHost 2.6's: more receivers of an event beside its on*().
 *
 * Each event keeps its one slot as before. The first listener of an event
 * makes its table here and puts the table's fan-out in that slot (the event's
 * _take(), CH32RVUsbHostHid.cpp / Midi.cpp; the device's _connected /
 * _disconnected), keeping the on*() callback that was there as the table's
 * primary; from then on on*() hands its callback to the table (_primary).
 * The code that delivers an event is not touched: a sketch that never adds a
 * listener links none of this, and an event no listener was added to links
 * no table (removeListener() walks only the tables in use). Measured
 * 2026-11-23: no listener +0 bytes, the first of an event about +260 flash
 * and +80 RAM (four slots), each more about +70 flash (its lambda).
 *
 * Normal context only (the slots change outside the interrupt, as on*()'s). */
#include "CH32RVUsbHost.h"

#if CH32RV_USB_HOST_STACK

#ifndef CH32RV_USB_HOST_MAX_LISTENERS_PER_EVENT
#define CH32RV_USB_HOST_MAX_LISTENERS_PER_EVENT 4
#endif

#define CH32RV_HOST_SLOT_DECL(name, Type)                                                          \
    extern void (*ch32rv_usb_host_##name##_primary)(const CH32RVUsbHost::Type &c);                 \
    CH32RVUsbHost::Type ch32rv_usb_host_##name##_take(const CH32RVUsbHost::Type &fanout);
CH32RV_HOST_SLOT_DECL(keyboard, KeyboardCallback)
CH32RV_HOST_SLOT_DECL(mouse, MouseCallback)
CH32RV_HOST_SLOT_DECL(consumer, ConsumerControlCallback)
CH32RV_HOST_SLOT_DECL(system, SystemControlCallback)
CH32RV_HOST_SLOT_DECL(gamepad, GamepadCallback)
CH32RV_HOST_SLOT_DECL(midi, MidiMessageCallback)

namespace {

/* The tables in use, for removeListener() */
/* every member initialised: the tables are constant-initialised, with no
 * static constructor to keep the unused ones */
struct TableBase {
    TableBase *next = nullptr;
    bool (*remove)(TableBase *t, CH32RVUsbHostListenerId id) = nullptr;
};
TableBase *tables;
CH32RVUsbHostListenerId next_id = 1;

template <typename Cb> struct Table : TableBase {
    struct Slot {
        CH32RVUsbHostListenerId id = CH32RV_USB_HOST_INVALID_LISTENER_ID;
        Cb cb;
    };
    Slot slots[CH32RV_USB_HOST_MAX_LISTENERS_PER_EVENT];
    Cb primary;   /* on*()'s */
    bool on = false;
};

Table<CH32RVUsbHost::KeyboardCallback> keyboard;
Table<CH32RVUsbHost::MouseCallback> mouse;
Table<CH32RVUsbHost::ConsumerControlCallback> consumer;
Table<CH32RVUsbHost::SystemControlCallback> system_control;
Table<CH32RVUsbHost::GamepadCallback> gamepad;
Table<CH32RVUsbHost::MidiMessageCallback> midi;
Table<CH32RVUsbHost::DeviceCallback> connected;
Table<CH32RVUsbHost::DeviceCallback> disconnected;

template <typename Cb, Table<Cb> &T, typename Ev> void deliver(const Ev &e)
{
    if (T.primary) {
        T.primary(e);
    }
    for (const auto &s : T.slots) {
        if (s.id) {
            s.cb(e);
        }
    }
}

template <typename Cb, Table<Cb> &T> void set_primary(const Cb &cb)
{
    T.primary = cb;
}

template <typename Cb> bool remove_from(TableBase *base, CH32RVUsbHostListenerId id)
{
    Table<Cb> *t = static_cast<Table<Cb> *>(base);
    for (auto &s : t->slots) {
        if (s.id == id) {
            s.id = CH32RV_USB_HOST_INVALID_LISTENER_ID;
            s.cb = nullptr;
            return true;
        }
    }
    return false;
}

/* A free slot of T for cb; the first time, T takes the event's slot over
 * (take(fan-out) returns the on*() callback there) and on*() is pointed at
 * it (primary). */
template <typename Cb, Table<Cb> &T, typename Ev, typename Take>
CH32RVUsbHostListenerId add(const Cb &cb, Take take, void (**primary)(const Cb &))
{
    if (!cb) {
        return CH32RV_USB_HOST_INVALID_LISTENER_ID;
    }
    for (auto &s : T.slots) {
        if (s.id) {
            continue;
        }
        if (!T.on) {
            T.primary = take(Cb(deliver<Cb, T, Ev>));
            *primary = set_primary<Cb, T>;
            T.remove = remove_from<Cb>;
            T.next = tables;
            tables = &T;
            T.on = true;
        }
        s.cb = cb;
        s.id = next_id++;
        if (next_id == CH32RV_USB_HOST_INVALID_LISTENER_ID) {
            next_id = 1;
        }
        return s.id;
    }
    return CH32RV_USB_HOST_INVALID_LISTENER_ID;
}

}   // namespace

CH32RVUsbHostListenerId CH32RVUsbHost::addKeyboardListener(KeyboardCallback callback)
{
    return add<KeyboardCallback, keyboard, CH32RVUsbHostKeyboardEvent>(callback, ch32rv_usb_host_keyboard_take,
                                                                       &ch32rv_usb_host_keyboard_primary);
}

CH32RVUsbHostListenerId CH32RVUsbHost::addMouseListener(MouseCallback callback)
{
    return add<MouseCallback, mouse, CH32RVUsbHostMouseEvent>(callback, ch32rv_usb_host_mouse_take,
                                                              &ch32rv_usb_host_mouse_primary);
}

CH32RVUsbHostListenerId CH32RVUsbHost::addConsumerControlListener(ConsumerControlCallback callback)
{
    return add<ConsumerControlCallback, consumer, CH32RVUsbHostConsumerControlEvent>(
        callback, ch32rv_usb_host_consumer_take, &ch32rv_usb_host_consumer_primary);
}

CH32RVUsbHostListenerId CH32RVUsbHost::addSystemControlListener(SystemControlCallback callback)
{
    return add<SystemControlCallback, system_control, CH32RVUsbHostSystemControlEvent>(
        callback, ch32rv_usb_host_system_take, &ch32rv_usb_host_system_primary);
}

CH32RVUsbHostListenerId CH32RVUsbHost::addGamepadListener(GamepadCallback callback)
{
    return add<GamepadCallback, gamepad, CH32RVUsbHostGamepadEvent>(callback, ch32rv_usb_host_gamepad_take,
                                                                    &ch32rv_usb_host_gamepad_primary);
}

CH32RVUsbHostListenerId CH32RVUsbHost::addMidiMessageListener(MidiMessageCallback callback)
{
    return add<MidiMessageCallback, midi, CH32RVUsbHostMidiMessage>(callback, ch32rv_usb_host_midi_take,
                                                                    &ch32rv_usb_host_midi_primary);
}

CH32RVUsbHostListenerId CH32RVUsbHost::addDeviceConnectedListener(DeviceCallback callback)
{
    return add<DeviceCallback, connected, CH32RVUsbHostDeviceInfo>(
        callback,
        [this](const DeviceCallback &fanout) {
            const DeviceCallback had = _connected;
            _connected = fanout;
            return had;
        },
        &ch32rv_usb_host_connected_primary);
}

CH32RVUsbHostListenerId CH32RVUsbHost::addDeviceDisconnectedListener(DeviceCallback callback)
{
    return add<DeviceCallback, disconnected, CH32RVUsbHostDeviceInfo>(
        callback,
        [this](const DeviceCallback &fanout) {
            const DeviceCallback had = _disconnected;
            _disconnected = fanout;
            return had;
        },
        &ch32rv_usb_host_disconnected_primary);
}

bool CH32RVUsbHost::removeListener(CH32RVUsbHostListenerId listenerId)
{
    if (listenerId == CH32RV_USB_HOST_INVALID_LISTENER_ID) {
        return false;
    }
    for (TableBase *t = tables; t; t = t->next) {
        if (t->remove(t, listenerId)) {
            return true;
        }
    }
    return false;
}

#else /* no host */

CH32RVUsbHostListenerId CH32RVUsbHost::addKeyboardListener(KeyboardCallback) { return 0; }
CH32RVUsbHostListenerId CH32RVUsbHost::addMouseListener(MouseCallback) { return 0; }
CH32RVUsbHostListenerId CH32RVUsbHost::addConsumerControlListener(ConsumerControlCallback) { return 0; }
CH32RVUsbHostListenerId CH32RVUsbHost::addSystemControlListener(SystemControlCallback) { return 0; }
CH32RVUsbHostListenerId CH32RVUsbHost::addGamepadListener(GamepadCallback) { return 0; }
CH32RVUsbHostListenerId CH32RVUsbHost::addMidiMessageListener(MidiMessageCallback) { return 0; }
CH32RVUsbHostListenerId CH32RVUsbHost::addDeviceConnectedListener(DeviceCallback) { return 0; }
CH32RVUsbHostListenerId CH32RVUsbHost::addDeviceDisconnectedListener(DeviceCallback) { return 0; }
bool CH32RVUsbHost::removeListener(CH32RVUsbHostListenerId) { return false; }

#endif
