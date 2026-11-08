<!-- Before publishing (contrib/pqch/RELEASE.md): check SHA256SUMS, sign it with the release key,
     attach SHA256SUMS.asc, and replace the "Changes" list below. This comment is not shown. -->
**PQCH {{VERSION}}**

**Changes**
- (fill in)

**Verify before installing**
1. Import the release key (`pqch-releases.asc`) and check its fingerprint:
   `1CDF A6A8 4A35 AA6D 7B52  4D1C FB6E 21DF 0D38 B5DA`
2. `gpg --verify SHA256SUMS.asc SHA256SUMS` → "Good signature from PQCH Releases"
3. Linux: `sha256sum --ignore-missing -c SHA256SUMS` · Windows: `certutil -hashfile pqch-{{VERSION}}-win64-setup.exe SHA256`, then compare with the line in `SHA256SUMS`.

The Windows installer is not code-signed, so SmartScreen shows "Unknown publisher". The GPG signature above is what proves the files are ours.
