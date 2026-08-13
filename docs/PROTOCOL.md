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

Stored in SSM at `/website/polaroid/device-secret`, baked into the firmware at
`firmware/include/Secrets.h` (gitignored; copy `Secrets.h.example`). One device, one secret, rotated
by hand if it ever leaks.

This is the same scheme the Spotify display uses, and the same code — `isDevice()` in
`api/auth.ts`. A static secret rather than a JWT because neither device has a clock worth trusting
an `exp` against.

The human-facing upload page uses the site's existing passkey auth and a `UserType.SHARE` token, so
the couple gets a link and never sees the device token.

## `POST /polaroid/photo`

One endpoint, two answers, discriminated by whether the body carries an id.

**`{}`** — metadata for every photo the device should be holding:

```json
{
  "version": 1,
  "photos": [
    { "id": "01HQ7X2K", "hash": "a3f2c1d4", "uploadedAt": 1755043200 },
    { "id": "01HQ7X9M", "hash": "9b7e0a12", "uploadedAt": 1755129600 }
  ]
}
```

Ordered oldest-first — display order, and the order `NORMAL` mode walks. Capped at the newest 50
(`MAX_PHOTOS`), so the device is never told about photos it has no room for. The device diffs this
against its local manifest to get three sets: fetch, delete, keep. Only `fetch` costs bandwidth.

**`{ "id": "01HQ7X2K" }`** — exactly 120,000 bytes, `application/octet-stream`, `Cache-Control: public, max-age=31536000,
immutable`. The bytes for a given `id` never change — a re-render gets a new `id`.

One request, no `Range`. `HTTPClient::writeToStream()` already streams the body to LittleFS in
~1.4 KB reads, so ranged chunks would buy no RAM — they would only buy a TLS handshake per chunk,
and a handshake is one to two seconds of ESP32 CPU at ~40 mA. Success is decided by the file being
exactly 120,000 bytes; a truncated body, a 404 or a dropped connection all land as a short file and
get retried on the next sync.

## Human endpoints

These are behind passkey auth, not the device token, and the device never calls them.

| | |
| --- | --- |
| `POST /polaroid/upload` | JPEG, PNG or HEIC body. Runs the pipeline, returns `{ id, previewUrl }`. No options — one film profile, and whatever editing the uploader already did is respected. |
| `GET /polaroid/photos` | list with preview URLs, for the manage page |
| `POST /polaroid/remove` | body `{ id }`; deletes both objects permanently, device drops it on next sync |

They require a `UserType.ADMIN` or `UserType.POLAROID_OWNER` token, and they are served from
`maxrosoff.com/polaroid`.

`previewUrl` is a **presigned S3 URL**, not a route. An `<img src>` has to be a plain GET, and
routing thumbnails through API Gateway would base64-inflate every one of them through a Lambda for
no benefit while the bucket stays private. They expire after an hour, which outlives any visit to
the page.

## There is no database

The photo list is a `ListObjectsV2` over the bucket. A table would hold id, hash and uploadedAt —
exactly the Key, ETag and LastModified that S3 already returns — so it would only add a second copy
of the truth that can disagree with the first. Every upload would become two writes that can
half-fail, leaving a manifest entry pointing at a framebuffer that isn't there, which is the one
failure the device cannot route around.

Ordering happens in the Lambda, since S3 lists lexicographically rather than by date. That is a
sort over a few dozen entries; it would be the wrong trade at a hundred thousand.

## What the device does with all this

```
wake
  └─ shake, or 24 h since last sync?
       ├─ no  → advance index, render, sleep          (no network at all)
       └─ yes → WiFi up
                POST /photo {}
                diff → { fetch, delete, keep }
                for each fetch: POST /photo {id} → LittleFS
                for each delete: unlink
                write local manifest
                if shake: jump to the newest uploadedAt
                WiFi down
                render, sleep
```

Failure is always "keep showing what's already there." A sync that can't reach the server, or gets
a 500, or times out, logs it and falls through to a normal render. There is no state in which the
panel goes blank because the network was unhappy.
