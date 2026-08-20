# Sample licenses

Five real, signed licenses so you can watch every state happen without emailing
anyone. Point either example at one:

```bash
RGCAD_LICENSE=examples/licenses/valid.lic ./rgcad          # the Qt CAD app
./01-minimal examples/licenses/wrong-machine.lic           # the console example
```

| File | What it demonstrates | Result |
|---|---|---|
| `valid.lic` | Both features present | **Pro** -- 3D and STL export unlocked |
| `stl-only.lic` | Only `cad_stl_export` | **Draft** -- 3D locked, STL export unlocked |
| `expired.lic` | Expiry in the past, no grace | **Draft** -- `License has expired` |
| `grace.lic` | Expired, inside a 30-day grace window | **Pro**, with days remaining reported |
| `wrong-machine.lic` | Node-locked to a different machine | **Draft** -- `Hardware mismatch: 0 of 4 required components matched` |

`stl-only.lic` is the one worth dwelling on. The license is completely valid and
its signature verifies; 3D is locked purely because `cad_3d` is absent from its
feature list. That is per-feature gating rather than two SKUs, and it is what
RockyGuard actually sells.

`grace.lic` is the second. It is expired, and Pro still works, and the app reports
how many days remain. A CAD tool must never hold a customer's drawing hostage
because a renewal is late -- the rule the code follows is **fail closed on
capability, fail open on data**.

## Two things these are NOT

**They are not node-locked.** `valid.lic`, `stl-only.lic`, `expired.lic` and
`grace.lic` were minted with `--threshold 0`, which means zero of the four
hardware components have to match, so they verify on any machine. That is what
makes them useful as samples and **exactly what you must never ship**: a
production node-locked license wants the default threshold of 2 (or higher), so
it binds to one machine while surviving ordinary hardware changes.
`wrong-machine.lic` uses `--threshold 4` to force the mismatch on purpose.

**They contain no real hardware.** The fingerprint field is
`0000...|1111...|2222...|3333...`, a placeholder. Nobody's machine identifiers are
committed here, which is checked before every commit -- a node-locked license
carries hashes of a real MAC address, CPU id, disk serial and motherboard id, and
those do not belong in a repository.

## Minting your own

You need a vendor license from Rocky Software, then your own keypair. The public
half goes in your binary; the private half never leaves your build machine.

```bash
license_keygen --private private.pem --public public.pem

license_create --vendor-license vendor_license.json --key private.pem \
  --id "LIC-001" --licensee "Customer Name" --product "YourApp" \
  --expires "2027-12-31T23:59:59Z" \
  --fingerprint-value "<their fingerprint from rg_fingerprint>" \
  --feature cad_3d --feature cad_stl_export \
  --output license.lic
```

The public key these samples verify against is in
[`../02-qt-cad/licensing/gate.cpp`](../02-qt-cad/licensing/gate.cpp). Replace it
with your own and these samples stop verifying, which is the point -- they are
signed by a key only this repo's samples use.

One nicety worth knowing: a license that expires within seven days of being minted
counts as an *evaluation mint* and does not consume your vendor license's
generation budget. So producing an `expired.lic` to exercise the expiry path is
free, and you are not punished for testing carefully.
