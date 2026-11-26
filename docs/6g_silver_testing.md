# 6G: Opening SBTesting / GotoTesting (tethered)

## What works

On Nano 6G (S5L8723, Silver UI), tapping a registered NanoApp can dispatch the stock `GotoTesting` action after the HRL1 app runs. This opens SBTesting. SBTesting is unstable on 6G and may reboot the device; this is not a NanoApps crash.

## How it was done

In the 6G resident tap/launch path (`system/silver_resident/silver_resident.c`), after a successful `hb_reloc_load` + app call returns, dispatch the controller action:

```c
typedef void *(*str_ctor_t)(void *out, const char *s);
typedef void (*str_dtor_t)(void *s);
typedef void (*action_t)(void *self, void *str, int a2);
char strbuf[16];
void *str;
str = ((str_ctor_t)(uintptr_t)0x0817fd6du)(strbuf, "GotoTesting");
if (str) {
    uint32_t obj = *(uint32_t *)self;          /* home controller object */
    uint32_t fn  = *(uint32_t *)(obj + 0x180u + 0x10u); /* vtable +0x190 */
    ((action_t)(uintptr_t)(fn | 1u))(self, str, 0);
    ((str_dtor_t)(uintptr_t)0x081892c1u)(str);
}
return;
```

## Address notes (6G, base 0x08000000)

- `0x0817fd6c` - OS string constructor used by the stock GotoTesting handler (observed at `0x080d6268` in decrypted `n6_os_image.bin`).
- `0x081892c0/0x081892c1` - string release/unlink used by rawmsg dispatch path.
- vtable slot `+0x190` on `*(uint32_t*)self + 0x180` - action dispatch, matches `0x080d6268` GotoTesting handler pattern.
- "GotoTesting" / "sbitem.SBTesting.clicked" strings present in OS image (e.g. file off 0x79721c for sbitem string).

## Caveats

- Provisional/reverse-engineered; verified by on-device tap opening SBTesting.
- Tethered RAM deploy only; reboot clears resident/hooks.
- SBTesting may reboot the device.
