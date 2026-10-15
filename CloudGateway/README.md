# Lumiac remote gateway

The Worker serves **https://lumiac.control.timiac.it/** with the existing Andrea
password account, a separate D1 database, and its own controller key. It exposes
authenticated, boot-bound command queues and SHA-256 firmware storage/jobs.

```powershell
npm ci
npm test
npm run check
npm run deploy
```

The configured deployment has `FIREBASE_API_KEY` and `LUMIAC_DEVICE_KEY` Worker
secrets. They are supplied separately with `wrangler secret bulk`; local secret
files are ignored. The controller uses its paired `src/cloud_config.h` settings.

`cloudflare/schema.sql` creates the database schema. The deploy configuration
uses the `timiac.it` Cloudflare zone and a dedicated custom domain/certificate.
See [installation and updates](../Docs/remote-updates.md) for hardware setup.
