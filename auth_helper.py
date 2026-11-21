import asyncio
import os
import json
from playwright.async_api import async_playwright

STATE_FILE = "session_state.json"
DB_FILE = "courses_db.json"

async def main():
    print("====================================")
    print("  CityU 資料獲取助手 (Python)  ")
    print("====================================")

    # 確保有一個空的資料庫檔案，避免 C++ 讀取失敗
    if not os.path.exists(DB_FILE):
        with open(DB_FILE, "w", encoding="utf-8") as f:
            json.dump({}, f)
        print(f"✅ 已建立空白資料庫 {DB_FILE}")

    async with async_playwright() as p:
        print("啟動瀏覽器...")
        browser = await p.chromium.launch(headless=False)
        
        # 載入之前的登入狀態
        if os.path.exists(STATE_FILE):
            print("✅ 載入歷史登入狀態。")
            context = await browser.new_context(storage_state=STATE_FILE, accept_downloads=True)
        else:
            print("⚠️ 找不到登入狀態，請準備手動登入。")
            context = await browser.new_context(accept_downloads=True)

        page = await context.new_page()

        # === 1. 抓 Degree Works ===
        print("\n[1/2] 前往 Degree Works...")
        await page.goto("https://degreeaudit.cityu.edu.hk:9040/dashboard/worksheets")
        await page.wait_for_timeout(3000)
        
        # 檢查是否被踢回登入頁
        if "login" in page.url.lower() or "auth.cityu" in page.url.lower():
            print("🔒 需要手動登入 Degree Works...")
            input(">>> 請在彈出的瀏覽器中完成登入 (包含 MFA)，看到成績單後按 Enter...")
            # 登入成功後，儲存狀態
            await context.storage_state(path=STATE_FILE)
            print("✅ 登入狀態已儲存。")
        
        # 抓取文字並存檔
        text = await page.evaluate("document.body.innerText")
        with open("page_text.txt", "w", encoding="utf-8") as f:
            f.write(text)
        print("✅ Degree Works 資料已儲存為 page_text.txt。")

        # === 2. 下載 SPARK Excel ===
        print("\n[2/2] 前往 SPARK 下載模板...")
        # 直接前往 submission 頁面
        await page.goto("https://csapp.cs.cityu.edu.hk/spark/student/submission")
        await page.wait_for_timeout(3000)
        
        # 如果 SPARK 要求重新登入（通常不用，因為 SSO 已通）
        if "login" in page.url.lower():
            print("🔒 SPARK 需要登入...")
            # 如果停在引導頁，模擬點擊 Login 按鈕
            try:
                await page.click("text=Login")
                await page.wait_for_timeout(3000)
            except:
                pass
        
        try:
            print("正在尋找下載按鈕...")
            async with page.expect_download() as download_info:
                # 尋找包含 'Download' 的按鈕或連結
                await page.click("text=Download")
            download = await download_info.value
            await download.save_as("spark_template.xlsx")
            print("✅ SPARK Excel 下載成功，儲存為 spark_template.xlsx。")
        except Exception as e:
            print(f"❌ SPARK 下載失敗: {e}")
            print("⚠️ 請手動下載 Excel，並重新命名為 spark_template.xlsx")

        await browser.close()
        print("\n🎉 所有前置資料獲取完畢！請執行 C++ 程式進行排課。")

if __name__ == "__main__":
    asyncio.run(main())
