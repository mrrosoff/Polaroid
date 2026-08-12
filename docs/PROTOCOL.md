# Device ⇄ server protocol

The device pulls. The server never reaches the device, never holds a connection open, and never
needs to know the device's address. This is what lets the thing sleep behind a NAT on someone
else's home WiFi for two months without anyone thinking about it.

Base URL: `https://api.maxrosoff.com/polaroid`

## Auth

Every device request carries a static token:

```
Authorization: Bearer <POLAROID_DEVICE_TOKEN>
```

The token is stored in SSM at `/polaroid/device-token` and baked into the firmware at
`firmware/include/Secrets.h` (gitignored; copy `Secrets.h.example`). One device, one token, rotated
by hand if it ever leaks. Deliberately not JWT: the device has no clock it trusts across a two-month
sleep, so anything with an `exp` would be a liability.

The human-facing upload page uses the site's existing passkey auth and a `UserType.SHARE` token, so
the couple gets a link and never sees the device token.

## `GET /polaroid/manifest`

The whole point of the sync. Small, cacheable, and enough to diff against without downloading
anything.

```json
{
  "version": 1,
  "photos": [
    { "id": "01HQ7X2K", "hash": "a3f2c1d4", "uploadedAt": 1755043200 },
    { "id": "01HQ7X9M", "hash": "9b7e0a12", "uploadedAt": 1755129600 }
  ]
}
```

Ordered oldest-first — that's display order, and it's also the order `NORMAL` mode walks. `hash` is
the first 8 hex of the SHA-256 of the *packed framebuffer*, not of the source JPEG, so re-tuning the
dither invalidates the cache correctly and re-cropping a photo does too.

The device compares this against its local manifest and computes three sets: fetch, delete, keep.
Only `fetch` costs bandwidth.

## `GET /polaroid/photo/{id}.bin`

Exactly 120,000 bytes, `application/octet-stream`, `Cache-Control: public, max-age=31536000,
immutable`. The bytes for a given `id` never change — a re-render gets a new `id`.

Supports `Range` requests. The device fetches in 8 KB chunks and writes each straight to LittleFS,
so a photo never exists in RAM and a dropped connection resumes rather than restarting.

## `GET /polaroid/recent?n=5`

```json
{ "photos": ["01HQ7X9M", "01HQ7X2K"] }
```

Most-recently-uploaded first. After a shake-triggered sync the device jumps to `photos[0]` so the
first thing you see is the photo that was just added — that's the whole shake gesture, end to end.

## Human endpoints

These are behind passkey auth, not the device token, and the device never calls them.

| | |
| --- | --- |
| `POST /polaroid/upload` | multipart JPEG/PNG/HEIC. Runs the pipeline, returns `{ id, previewUrl }`. |
| `GET /polaroid/photos` | list with preview URLs, for the manage page |
| `DELETE /polaroid/photo/{id}` | removes from the manifest; device drops it on next sync |
| `PATCH /polaroid/photo/{id}` | `{ caption, crop, filmStock }` — re-renders, returns a new `id` |

## What the device does with all this

```
wake
  └─ shake, or 24 h since last sync?
       ├─ no  → advance index, render, sleep          (no network at all)
       └─ yes → WiFi up
                GET /manifest
                diff → { fetch, delete, keep }
                for each fetch: GET /photo/{id}.bin → LittleFS
                for each delete: unlink
                write local manifest
                if shake: GET /recent?n=1, jump to it
                WiFi down
                render, sleep
```

Failure is always "keep showing what's already there." A sync that can't reach the server, or gets
a 500, or times out, logs it and falls through to a normal render. There is no state in which the
panel goes blank because the network was unhappy.
