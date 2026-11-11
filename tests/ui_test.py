import asyncio, os, glob
from playwright.async_api import async_playwright
P=open('/tmp/bvport.txt').read().strip()
async def main():
    async with async_playwright() as p:
        b = await p.chromium.launch(executable_path="/usr/bin/google-chrome", args=["--autoplay-policy=no-user-gesture-required","--no-sandbox"])
        pg = await b.new_page(viewport={"width":1240,"height":780})
        logs=[]; pg.on("console", lambda m: logs.append(m.text)); pg.on("pageerror", lambda e: logs.append("PAGEERROR "+str(e)))
        await pg.goto(f"http://localhost:{P}/"); await pg.wait_for_timeout(2500)
        print("count:", await pg.text_content("#count"))
        print("gstatus:", await pg.text_content("#gstatus"), "bg ready", await pg.evaluate("bg.readyState"))
        await pg.screenshot(path="/tmp/s1.png")
        # add two to reel
        adds = await pg.query_selector_all("[data-add]"); print("add buttons", len(adds))
        await pg.click("[data-add=\"0\"]"); await pg.click("[data-add=\"2\"]")
        await pg.wait_for_timeout(1500)
        print("reel:", await pg.text_content("#reelcount"), await pg.inner_text("#reel"))
        # move second up
        await pg.click('[data-mv="1:-1"]'); print("reordered:", await pg.inner_text("#reel"))
        await pg.click("#playreel"); await pg.wait_for_timeout(1200)
        print("now:", await pg.text_content("#nowwho"), "| playing:", not await pg.evaluate("audio.paused"))
        await pg.screenshot(path="/tmp/s2.png")
        await pg.click("#export"); await pg.wait_for_timeout(1000)
        await pg.screenshot(path="/tmp/s3.png")
        print("rec visible:", await pg.is_visible("#rec"), await pg.text_content("#rectext"), "canvas", await pg.evaluate("[stage.width,stage.height]"))
        for _ in range(120):
            await pg.wait_for_timeout(500)
            st = await pg.text_content("#estatus")
            if st.startswith("Saved") or "fail" in st: break
        print("estatus:", st)
        # format switch
        await pg.select_option("#fmt","horizontal"); await pg.wait_for_timeout(500); await pg.screenshot(path="/tmp/s4.png")
        print("logs:", logs[-8:])
        await b.close()
asyncio.run(main())
