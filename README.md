# CityU Automated Study Plan Generator

This project is a C++17 command-line tool that builds a customizable Excel study plan from Degree Works requirements and shared course-catalogue data.

## Build

Requirements: CMake 3.20+, a C++17 compiler, libcurl, Python 3, and `openpyxl` for the Excel-export smoke test. Playwright and Chromium are only needed for live Degree Works login.

```bash
sudo apt install cmake g++ libcurl4-openssl-dev  # Debian/Ubuntu
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade playwright openpyxl
.venv/bin/python -m playwright install chromium
sh scripts/build-and-test.sh
./build/cityu_study_plan
```

macOS: install CMake, a C++ compiler, and libcurl with Homebrew. Create `.venv` with `python3 -m venv .venv`, then install Playwright and `openpyxl` and Chromium using the commands above before running the build script. Windows: install Visual Studio C++ Build Tools, CMake, and vcpkg; set `VCPKG_ROOT`, then run `.\scripts\build-and-test.ps1`. For login and Excel export, create `.venv` with `py -m venv .venv`, then run `.venv\Scripts\python.exe -m pip install --upgrade playwright openpyxl` and `.venv\Scripts\python.exe -m playwright install chromium`. The vcpkg manifest installs libcurl. The application automatically prefers the project `.venv` when present, avoiding mismatches between system Python packages and Playwright's Node driver.

To check the Python/Playwright setup independently, run `.venv/bin/python auth_helper.py --check` on macOS/Linux or `.venv\Scripts\python.exe auth_helper.py --check` on Windows. The check verifies that Playwright's matching Node driver starts; install Chromium with the command it prints before logging in.

At runtime, the executable reads `config.json`, `courses_db.json`, `auth_helper.py`, and `excel_writer.py` beside the executable (or in the project folder above it), but writes only to the current user's application-data directory. This works when launched from any working directory and does not require the project or executable folder to be writable. The app-data directory is `%LOCALAPPDATA%\CityUStudyPlan` on Windows, `~/Library/Application Support/CityUStudyPlan` on macOS, and `$XDG_DATA_HOME/cityu-study-plan` (or `~/.local/share/cityu-study-plan`) on Linux. Share or copy the complete project folder, not just the executable.

## Run

```bash
./build/cityu_study_plan
./build/cityu_study_plan --mode 1
./build/cityu_study_plan --mode 2
./build/cityu_study_plan --dry-run
./build/cityu_study_plan --doctor
./build/cityu_study_plan --demo
./build/cityu_study_plan --demo --dry-run
./build/cityu_study_plan --database CityuCources.txt --import-only
./build/cityu_study_plan --database CityuCources.txt
./build/cityu_study_plan --database --fetch-only
```

- `--mode 1`: start planning, preferring local course data.
- `--mode 2`: scan and refresh only course codes present in the Degree Works audit, then prompt to continue planning.
- `--dry-run`: print the computed plan without writing Excel.
- `--refresh` / `--no-cache`: ignore page cache.
- `--doctor`: check required files and Python support.
- `--demo`: create an offline workbook from real course records in the bundled `courses_db.json`. It selects one course only as a catalogue example; it does not invent required courses or claim to represent a student's degree plan. It skips student files, login, and network access; the workbook is written to the `Study Plans` folder inside the per-user application-data directory, and the full path is printed on success.
- `--demo --dry-run`: print the example plan without writing Excel.
- `--database [file]`: import a pasted CityU catalogue text file, then optionally fill missing course pages.
- `--import-only`: use with `--database` to import without any network requests.
- `--fetch-only`: use with `--database` to fetch missing entries in the existing `courses_db.json` without importing a source file.

The startup menu offers **Start planning!** or **Scan for course updates**. The update scan first reads the saved Degree Works audit (and requests login if needed), then refreshes only unique course codes found in that audit, including its completed, in-progress, required, and listed choice candidates. It does not scan the whole catalogue, choose electives, or create a workbook during the scan step. Before making requests it reports an approximate duration and asks permission; requests are paced at least 60 seconds apart plus up to 30 seconds of jitter. The light-hearted “Maybe take a nap first?” note is only an estimate—the wait depends on catalogue response times. Press Ctrl+C to stop; each successful course update is saved as it completes to a separate `courses_db_scanned.json` in the user's application-data directory. The old `courses_db.json` remains unchanged by the scan. On completion, type `1` at the continuation prompt to proceed to planning in the same run.

During planning, course details in `courses_db_scanned.json` take precedence. Missing fields are filled from the older `courses_db.json`; only if the combined local records remain incomplete does the app offer an online lookup after showing its estimate and asking permission. Planning-time fetches are saved to the scanned database when it exists, otherwise to the regular personal database. If only offering terms are missing, planning uses the Semester A/B assumption without requiring a lookup. Pages are cached for 30 days, and all further requests stop for the run on a 403/429 or challenge page; a 24-hour cooldown prevents immediate retries. A scan that hits a cooldown reports that no requests were started; individual failures and save errors are reported and logged, while successful updates remain saved. The app does not attempt to bypass access controls, and no automated strategy can guarantee that a site will not block access. When lookup is unavailable or cancelled, the app continues with local information and writes a provisional workbook with unresolved items and warnings instead of silently treating missing data as verified.

The database builder accepts copied catalogue text containing the course index and page text. It saves the index and detailed course records to `courses_db.json`; course pages marked “Indicative only” remain flagged unconfirmed. For remaining codes, it shows the request count and time estimate before asking permission, sends at most one request at a time with the same 60–90 second pacing, disables retries, and stops on a block/challenge. Completed records are saved as the run progresses. The default source filename is `CityuCources.txt`; pass another path after `--database` if needed.

The Degree Works parser reads student/programme metadata, expected graduation and audit dates, explicit in-progress course rows, course titles, credit units, and semester terms. It derives the planning horizon and current semester when those fields are present; it asks only for missing scheduling information. Completed requirements are not scheduled again, and an in-progress course is placed in its reported term.

The terminal UI uses color only when attached to a terminal and `NO_COLOR` is not set. Prompts preserve typed URL casing, validate numeric answers, confirm destructive cleanup, and explain available commands. `M` prints the next page of up to 20 electives. `P` returns to the previous preference question where supported. `CLEAN` removes cached pages but preserves an active catalogue cooldown. Each elective section states the matching Degree Works choice and asks only for the courses needed to fill it. Missing credit values default to 3 and are marked as assumed; incomplete course records remain selectable with an official information URL.

The elective picker shows up to 20 candidates per page, including prerequisite status and the official course URL, with extra spacing between course details. Area requirements are grouped into Area 1/2/3 sections; candidate numbers follow the displayed order, and selections resolve against those exact rows. A selection must use distinct courses and meet the exact credit total; area requirements also enforce their listed-area and minimum-per-area rules. Degree Works completed and in-progress courses are matched to prerequisites; required non-elective and elective courses are considered planned, and only prerequisites absent from the degree audit are flagged. Prerequisite readiness is the primary ranking criterion, followed by topic and assessment preferences. Plans use the Degree Works horizon. In-progress courses are placed in their reported terms, required courses are spread across available Semester A/B terms to balance credits, and Project courses remain in the final year. Summer boxes are intentionally left for manual planning; Internship and Summer-only courses appear on a separate Manual Placement sheet. The scheduler avoids repeating courses and keeps LC0200A and LC0200B in separate semesters. If a term exceeds the preferred workload target, the CLI identifies alternatives offered in another semester and lets the user revise electives. Successfully fetched catalogue details are merged into `courses_db.json`; incomplete local entries remain eligible for a cautious lookup.

When the catalogue says `Not offered`, uses `X`, or has no offering terms, planning provisionally treats the course as available in Semester A and Semester B (never Summer) and adds a visible warning; the shared catalogue data is not changed to create this assumption. When an elective or requirement has incomplete details, the CLI says it is trying to get information for that specific course. If lookup is unavailable or cancelled, the course remains visible with an official information URL and unresolved details are not invented. Missing credit values default to 3 and are visibly marked as assumed.

The output workbook is generated independently of the SPARK template. Academic years are separated vertically; Semester A, Semester B, and Summer remain horizontal. Each semester uses separate Course Code, Course Name, Credits, and Remarks columns, with clearly outlined year and semester blocks including their headers. Every course has its own row, semester and year credit totals are shown, and Summer is reserved for manual entry. Courses that cannot be planned automatically are retained on the Manual Placement sheet. A warning banner and concise per-course remarks identify assumptions or prerequisite conflicts; known missing prerequisites are named, while courses already studied, in progress, or planned do not receive a missing-prerequisite warning. The workbook contains no subject dropdowns or programme-specific graduation rules. A notes sheet reminds students to verify requirements and prerequisites. C/Q also remove a legacy downloaded SPARK template if one remains from an earlier version.

## Authentication helper contract

`auth_helper.py` is invoked by C++ to read Degree Works:

```bash
python3 auth_helper.py --state session_state.json --page-text page_text.txt --courses-db courses_db.json
```

Exit codes:

| Code | Meaning |
|---|---|
| 0 | Helper completed |
| 21 | Degree Works/session failure |
| 130 | User cancelled |

The helper opens a visible browser for SAML/Okta manual login and MFA. C++ never receives or stores passwords.

## Commands

Every prompt shows `OK/C/FIX/Q/P/M/CLEAN` as applicable. `OK` proceeds, `C` deletes `session_state.json` and `page_text.txt`, `Q` confirms and quits, `P` goes back, `M` loads more ranked courses, and `FIX` repairs a single course fetch. `CLEAN` is reserved for interactive cleanup of cache, logs, and overrides after confirmation.

## Privacy and deletion

Credentials/sessions are stored locally in `session_state.json`. Degree Works text is in `page_text.txt`. Passwords are never written by C++ or the helper. These files, the personal copy of `courses_db.json`, generated workbooks, temporary plan data, cache, logs, and URL overrides live under the per-user application-data directory. C and Q delete the sensitive session and Degree Works files there; cache, logs, and overrides remain unless separate cleanup is requested. Cache is under `.cityu_cache/`, logs under `.cityu_logs/`, and overrides are in `overrides.json`.

## Error codes

| Range | Plain-language class |
|---|---|
| `AUTH-*` | Login/session authentication |
| `FETCH-*` | Catalogue network, 403/429, or challenge failure |
| `PARSE-*` | Page or course fields could not be parsed |
| `FIX-*` | User override saved |
| `COMPUTE-*` | Scheduling failure |
| `OUTPUT-*` | Excel/file failure |

Logs are written at `.cityu_logs/errors.log` in the per-user application-data directory with a plain sentence, suggested action, and technical detail.

## Scoring

Prerequisite readiness is the primary ranking factor (base scores: SATISFIED 4000, SCHEDULED 3000, UNKNOWN 2000, UNSCHEDULED 1000, on top of a shared 10000 baseline). A matching topic the user marked as not preferred subtracts 100; each matching assessment preference subtracts 10. Thus user preferences adjust order within readiness tiers rather than outweighing a prerequisite-readiness tier. Semester availability is used by scheduling and overload review, not as a preference score. No score floor is used; candidates are paged in groups of up to 20.

## `courses_db.json` schema

The top-level object maps course codes to course objects:

```json
{
  "CS3402": {
    "title": "Database Systems",
    "credits": 3,
    "semesters": ["Semester A", "Semester B"],
    "prerequisites": ["CS2311"],
    "prerequisites_text": "CS2311",
    "catalogue_year": "2026/27",
    "catalogue_detail_available": true,
    "area": "engineering",
    "tags": ["database", "systems"],
    "url": "https://www.cityu.edu.hk/catalogue/ug/current/course/CS3402.htm",
    "exam_percent": null,
    "other_percent": null,
    "raw_items": [],
    "raw_text": "",
    "assessment_unknown": true
  }
}
```

`exam_percent` and `other_percent` are the stored assessment percentages. The second value is displayed to students as continuous assessment and combines quizzes, coursework, attendance, projects, and other non-exam components; `raw_items` can list the source components without scoring them as separate categories. Percentages are nullable integers from 0–100. `assessment_unknown=true` means the split is unconfirmed and no default is invented. Legacy `ca_percent` fields are read as `other_percent`. Prerequisites is a list of course codes or `[]`; unknown prerequisite information is represented by `["UNKNOWN"]` or an omitted list, but never guessed. `exclusive_courses` records courses that must not both count toward the same plan, and `catalogue_notes` preserves relevant course-specific constraints. Conflicting exclusive courses prevent workbook export until the plan is corrected.

`prerequisites_text` preserves the source wording when prerequisites are conditional or use alternatives that the planner cannot safely model as a simple list. `catalogue_year` identifies the edition represented by an imported or fetched record.

The bundled `courses_db.json` is the shared reference catalogue. On first run, it is copied to the user's application-data directory. On later runs, missing bundled entries and bundled detailed records refresh incomplete or older local records, while newer personal catalogue records and local-only courses are preserved. Catalogue imports and successful lookups update only the personal copy, never the project resources. The optional `courses_db_scanned.json` stores scan-specific results separately; it is loaded first for planning and the regular database fills only information absent from those results.

The local database reader also accepts the prior browser script's format (`title`, `credits`, `area`, `semesters`, string `prerequisites`, `has_exam`, and `has_group_work`). Legacy exam booleans are not treated as assessment percentages, and blank or unparseable prerequisite text remains `UNKNOWN`. This supports importing an already-created local database without running a challenge-clearing browser scraper.

## Overrides

`overrides.json` is JSON Lines (`.jsonl` format), with one object per line:

```json
{"course":"CS3402","url":"https://example.invalid/correct-course-page","timestamp":"2026-11-21T14:05:22+08:00"}
```
## Excel Export

The generated Excel workbook provides a structured and readable study plan based on the planning results.

### Features

- Organizes courses by academic year and semester.
- Supports courses spanning multiple semesters.
- Calculates semester credits, yearly credits, total scheduled credits, and degree credits separately.
- Highlights missing prerequisite requirements.
- Provides separate worksheets for manual course placement, elective selections, and additional notes.
- Improves readability through consistent formatting, adjustable row heights, and warning highlights.
- Supports clickable course links and print-friendly layouts.

The existing JSON interface is preserved to maintain compatibility with the C++ planning system.

## Excel Regression Testing

Run the following command from the project root:

```bash
python3 tests/test_excel_export.py
```

The tests verify:

1. Cross-semester course placement and credit calculations.
2. Prerequisite remarks and warning formatting.
3. Manual course placement and workbook layout.

These tests use simulated course data and do not require an internet connection or Degree Works authentication.

## Excel Export
## Excel Regression Testing
