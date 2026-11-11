#!/usr/bin/env python3
"""Cross-builds 'Brainrot Voice Memo.app' (universal arm64 + x86_64), signs it ad-hoc, and zips it."""
import base64, hashlib, os, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
import sign

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "out")
APP = os.path.join(OUT, "Brainrot Voice Memo.app")
EXE = "BrainrotVoicemail"
IDENT = "com.hank.brainrotvoicemail"
VERSION = "1.3.1"

INFO_PLIST = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key><string>en</string>
	<key>CFBundleDisplayName</key><string>Brainrot Voice Memo</string>
	<key>CFBundleExecutable</key><string>{EXE}</string>
	<key>CFBundleIconFile</key><string>AppIcon</string>
	<key>CFBundleIdentifier</key><string>{IDENT}</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key><string>Brainrot Voice Memo</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleShortVersionString</key><string>{VERSION}</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>LSApplicationCategoryType</key><string>public.app-category.entertainment</string>
	<key>LSMinimumSystemVersion</key><string>11.0</string>
	<key>NSHighResolutionCapable</key><true/>
	<key>NSPrincipalClass</key><string>NSApplication</string>
	<key>NSSpeechRecognitionUsageDescription</key><string>Brainrot Voice Memo transcribes your voice messages on this Mac to make captions.</string>
	<key>NSAppTransportSecurity</key>
	<dict>
		<key>NSAllowsLocalNetworking</key><true/>
		<key>NSAllowsArbitraryLoadsInWebContent</key><true/>
	</dict>
</dict>
</plist>
""".encode()

RULES = """	<key>rules</key>
	<dict>
		<key>^Resources/</key><true/>
		<key>^Resources/.*\\.lproj/</key><dict><key>optional</key><true/><key>weight</key><real>1000</real></dict>
		<key>^Resources/.*\\.lproj/locversion.plist$</key><dict><key>omit</key><true/><key>weight</key><real>1100</real></dict>
		<key>^Resources/Base\\.lproj/</key><dict><key>weight</key><real>1010</real></dict>
		<key>^version.plist$</key><true/>
	</dict>
	<key>rules2</key>
	<dict>
		<key>.*\\.dSYM($|/)</key><dict><key>weight</key><real>11</real></dict>
		<key>^(.*/)?\\.DS_Store$</key><dict><key>omit</key><true/><key>weight</key><real>2000</real></dict>
		<key>^(Frameworks|SharedFrameworks|PlugIns|Plug-ins|XPCServices|Helpers|MacOS|Library/(Automator|Spotlight|LoginItems))/</key><dict><key>nested</key><true/><key>weight</key><real>10</real></dict>
		<key>^.*</key><true/>
		<key>^Info\\.plist$</key><dict><key>omit</key><true/><key>weight</key><real>20</real></dict>
		<key>^PkgInfo$</key><dict><key>omit</key><true/><key>weight</key><real>20</real></dict>
		<key>^Resources/</key><dict><key>weight</key><real>20</real></dict>
		<key>^Resources/.*\\.lproj/</key><dict><key>optional</key><true/><key>weight</key><real>1000</real></dict>
		<key>^Resources/.*\\.lproj/locversion.plist$</key><dict><key>omit</key><true/><key>weight</key><real>1100</real></dict>
		<key>^Resources/Base\\.lproj/</key><dict><key>weight</key><real>1010</real></dict>
		<key>^[^/]+$</key><dict><key>nested</key><true/><key>weight</key><real>10</real></dict>
		<key>^embedded\\.provisionprofile$</key><dict><key>weight</key><real>20</real></dict>
		<key>^version\\.plist$</key><dict><key>weight</key><real>20</real></dict>
	</dict>
"""


def code_resources(files):
    b64 = lambda d: base64.b64encode(d).decode()
    f1 = "".join(f"\t\t<key>{n}</key>\n\t\t<data>{b64(hashlib.sha1(d).digest())}</data>\n" for n, d in files)
    f2 = "".join(f"\t\t<key>{n}</key>\n\t\t<dict>\n\t\t\t<key>hash</key>\n\t\t\t<data>{b64(hashlib.sha1(d).digest())}</data>\n"
                 f"\t\t\t<key>hash2</key>\n\t\t\t<data>{b64(hashlib.sha256(d).digest())}</data>\n\t\t</dict>\n" for n, d in files)
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
            '<plist version="1.0">\n<dict>\n'
            f'\t<key>files</key>\n\t<dict>\n{f1}\t</dict>\n\t<key>files2</key>\n\t<dict>\n{f2}\t</dict>\n'
            f'{RULES}</dict>\n</plist>\n').encode()


def sh(*cmd):
    subprocess.run(cmd, check=True, cwd=ROOT)


def main():
    # embed the UI
    html = open(os.path.join(ROOT, "src/index.html"), "rb").read()
    rows = ",\n".join(",".join(str(x) for x in html[i:i + 24]) for i in range(0, len(html), 24))
    open(os.path.join(ROOT, "src/html.c"), "w").write(
        f"#include <stddef.h>\nconst char INDEX_HTML[] = {{\n{rows},0}};\nconst size_t INDEX_HTML_LEN = {len(html)};\n")

    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(os.path.join(APP, "Contents/MacOS"))
    os.makedirs(os.path.join(APP, "Contents/Resources"))
    icns = open(os.path.join(ROOT, "assets/AppIcon.icns"), "rb").read()
    open(os.path.join(APP, "Contents/Resources/AppIcon.icns"), "wb").write(icns)
    open(os.path.join(APP, "Contents/Info.plist"), "wb").write(INFO_PLIST)
    open(os.path.join(APP, "Contents/PkgInfo"), "wb").write(b"APPL????")
    res = code_resources([("Resources/AppIcon.icns", icns)])
    os.makedirs(os.path.join(APP, "Contents/_CodeSignature"))
    open(os.path.join(APP, "Contents/_CodeSignature/CodeResources"), "wb").write(res)

    tbds = [f"sdk/{n}.tbd" for n in ("libSystem", "libobjc", "libsqlite3", "Foundation", "AppKit", "WebKit", "Speech", "AVFoundation")]
    slices = []
    for arch in ("arm64", "x86_64"):
        for src in ("main", "html"):
            sh("clang", "-target", f"{arch}-apple-macos11", "-ffreestanding", "-fno-builtin", "-O2", "-Wall",
               "-c", f"src/{src}.c", "-o", f"out/{src}.{arch}.o")
    # libSystem stub = every undefined C symbol that isn't objc/sqlite/our own
    und = set()
    for arch in ("arm64", "x86_64"):
        out = subprocess.run(["llvm-nm", "-u", f"out/main.{arch}.o"], capture_output=True, text=True, cwd=ROOT).stdout
        und |= {l.strip() for l in out.split() if l.strip()}
    libc = sorted(x for x in und if not x.startswith(("_objc_", "_sel_", "_class_", "_sqlite3_", "_INDEX_HTML")))
    open(os.path.join(ROOT, "sdk/libSystem.tbd"), "w").write(
        "--- !tapi-tbd\ntbd-version:     4\ntargets:         [ x86_64-macos, arm64-macos ]\n"
        "install-name:    '/usr/lib/libSystem.B.dylib'\ncurrent-version: 1\nexports:\n"
        "  - targets:         [ x86_64-macos, arm64-macos ]\n"
        f"    symbols:         [ {', '.join(libc + ['dyld_stub_binder'])} ]\n...\n")
    for arch in ("arm64", "x86_64"):
        raw = f"out/{EXE}.{arch}.raw"
        sh("ld64.lld-18", "-arch", arch, "-platform_version", "macos", "11.0", "14.0", "-adhoc_codesign", "-dead_strip",
           "-headerpad_max_install_names", "-o", raw, f"out/main.{arch}.o", f"out/html.{arch}.o", *tbds)
        signed = sign.sign(open(os.path.join(ROOT, raw), "rb").read(), IDENT, INFO_PLIST, res)
        path = os.path.join(OUT, f"{EXE}.{arch}")
        open(path, "wb").write(signed)
        slices.append(path)
    exe = os.path.join(APP, "Contents/MacOS", EXE)
    sh("llvm-lipo-18", "-create", *slices, "-output", exe)
    os.chmod(exe, 0o755)

    zipname = "Brainrot-Voice-Memo-mac.zip"
    subprocess.run(["zip", "-qry", os.path.join(OUT, zipname), "Brainrot Voice Memo.app"], check=True, cwd=OUT)
    print("built", os.path.join(OUT, zipname))


if __name__ == "__main__":
    main()
