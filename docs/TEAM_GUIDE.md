# StudyPlanner Team and AI Handoff Guide

## What this program does

StudyPlanner is a portable C++17 command-line application that reads a student's Degree Works audit, combines it with a shared CityU course catalogue, asks the student to choose elective courses, plans remaining courses by semester, and writes an Excel workbook. Python is used for browser-based manual Degree Works login and for workbook generation. The app should work from a copied project folder on Windows, macOS, and Linux, and should not require write access to its install folder.

The program is not an official degree-audit system. Degree Works requirements and the catalogue remain the source data; the planner makes a proposed schedule and clearly reports assumptions. It must not invent course details, guarantee enrolment, or bypass authentication, MFA, rate limits, or website controls.

## Start here

Before editing, inspect this guide, `README.md`, the target module, its public header, and the relevant tests. Build and test using:

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

The project test suite includes C++ unit tests, an offline demo-workbook test, and synthetic end-to-end CLI flows. Python workbook tests need `openpyxl`; live login additionally needs Playwright and Chromium, but normal tests must not use a real student account or the network.

## Current module boundaries

| Area | Primary files | Ownership |
|---|---|---|
| Application flow and integration | `src/main.cpp`, `src/ui.*` | CLI stages, prompts, parsing audit flow, orchestrating modules |
| Shared models/config/runtime | `src/models.*`, `src/config.*`, `src/runtime_paths.*`, `src/json.*` | Shared data contracts, config, portable paths and JSON |
| Catalogue parsing/acquisition | `src/page_parser.*`, `src/acquisition.*`, `src/database_builder.*` | Parse catalogue records, consent/rate-limit network lookups, import/fill catalogue |
| Audit course-update scan | `src/course_update_scan.*` | Audit-only refresh scan, separate scanned database, progress/errors/continuation |
| Elective logic | `src/electives.*` | Candidate ranking, choice validation, area grouping, interactive selection |
| Planning | `src/planner.*` | Prerequisite classification, semester scheduling, warnings, plan output model |
| Workbook | `src/excel.*`, `excel_writer.py` | Export data bridge, workbook sheets/content/style |
| Authentication | `auth_helper.py` | Visible manual browser login and extraction of Degree Works page text |
| Data and tests | `config.json`, `courses_db.json`, `ge_areas.json`, `tests/` | Shared catalogue/config and regression coverage |

Public C++ contracts are in headers. `Course`, `ChoiceGroup`, `DegreeRequirements`, `PlanCourse`, and `Plan` in `src/models.h` / `src/planner.h` are shared integration boundaries. Coordinate before changing these types or the JSON/data contracts.

## Team ownership and ready-to-use prompts

Give each contributor one prompt and ask them to work only within the assigned area. If a necessary cross-module change is unavoidable, ask first or make the smallest compatible change and clearly report it. Do not have multiple contributors modify shared files at the same time.

### A — Core application and integration

**Owns:** `src/main.cpp`, `src/ui.*`, shared integration, build/test integration where approved.

**Prompt for A:**

> You own the StudyPlanner base application and integration. Read `README.md` and `docs/TEAM_GUIDE.md` first. The program is a portable C++17 command-line Degree Works study planner; it reads an audit, looks up CityU course details, gathers elective preferences/selections, schedules remaining courses, and exports an Excel workbook. Preserve existing behavior and use the public headers as module boundaries. Coordinate before changing `Course`, `ChoiceGroup`, `DegreeRequirements`, `PlanCourse`, or `Plan`, JSON contracts, CLI prompts, or build wiring because B–E depend on them. Do not place database parsing, elective ranking, planner algorithms, workbook styling, or browser automation back into `main.cpp`; delegate those changes to their owner modules. Keep student data, credentials, machine paths, generated workbooks, runtime databases, and secrets out of Git. Run the relevant tests and report exact changed files and results.

### B — Course database and catalogue completeness

**Owns:** `src/database_builder.*`, `src/page_parser.*`, catalogue data updates when explicitly assigned, and related database tests.

**Prompt for B:**

> You own catalogue ingestion and the shared course database. Read `README.md` and `docs/TEAM_GUIDE.md`. Improve the catalogue builder so it reliably imports pasted catalogue text and requests details only for records that are still incomplete; preserve completed records and report skipped, malformed, and remaining counts clearly. Never make a network request without the existing user-consent flow, conservative pacing, and block/cooldown behavior. Do not fabricate course details or change database offering values to make planning easier: `Not offered` and absent terms are interpreted provisionally by planner logic, not rewritten in catalogue data. Keep unknown fields unknown, retain provenance/year, and avoid student-specific data. Add offline parser/builder tests with synthetic fixture data and no live requests. Coordinate any shared model or schema changes before editing.

### C — Excel output and workbook presentation

**Owns:** `src/excel.*`, `excel_writer.py`, workbook-specific tests.

**Prompt for C:**

> You own StudyPlanner's workbook export, content, and formatting. Read `README.md` and `docs/TEAM_GUIDE.md`. Preserve the existing export JSON contract unless you coordinate a change with A and D. The workbook must clearly outline every year and each Semester A/B/Summer block, including semester headings and column headings. Use clear per-course remarks: name known missing prerequisite course codes; do not warn when prerequisites are already studied, in progress, or planned; courses currently in progress must have blank remarks and no prerequisite note. Preserve unplanned courses on the manual-placement sheet and avoid inventing information. Test styles and workbook values with synthetic plans; never use or commit a real student's audit/workbook.

### D — Elective ranking and selection

**Owns:** `src/electives.*`, elective-related unit and CLI tests.

**Prompt for D:**

> You own elective selection behavior. Read `README.md` and `docs/TEAM_GUIDE.md`. Keep ranking, validation, area handling, and picker UI in `src/electives.*`; do not move this logic back into `main.cpp`. Selections must use distinct courses and meet exact course-count/credit requirements. For Degree Works area requirements, respect the listed areas and minimum credits required from distinct areas, and show clear Area 1/2/3 subsections. Rank prerequisite readiness before user topic/assessment preferences; assessment matching must honor known assessment components and must not infer unknown assessment data. Missing credits default to 3 only as an explicit assumption. Keep not-offered/missing-offering candidate handling consistent with the shared policy below without mutating catalogue records. Add focused tests for edge cases and synthetic CLI selections.

### E — Course update scanning and reliability

**Owns:** `src/course_update_scan.*`, dynamic acquisition and resilience in `src/acquisition.*`, `src/runtime_paths.*`, runtime config, and `auth_helper.py` where assigned; scan and integration tests.

**Prompt for E:**

> You own the Degree Works course-update scan and the reliability review of its surrounding code. The scan writes to the separate per-user `courses_db_scanned.json`, never overwriting the older `courses_db.json`. Read `README.md` and `docs/TEAM_GUIDE.md`. Verify that the scan refreshes only unique course codes actually present in the parsed audit, estimates duration before asking permission, saves each successful result, handles cache/database errors, network failures, cooldowns, 403/429 responses, malformed pages, cancellation, and partial completion without corrupting local data or silently claiming success. Planning must read scanned data first and fill missing information from the older catalogue only. Confirm scanning returns to the running program and does not start elective selection or workbook generation until the user types 1. Add offline synthetic tests for scan scope, primary/fallback precedence, cooldown/no-network behavior, failures, and continuation; never use real student accounts or the live catalogue in tests. Preserve the visible manual browser login/MFA flow; never store or request passwords in C++, bypass access controls, or evade blocks/rate limits. Keep app data, cache, logs, temporary files, and generated workbooks in per-user writable locations. Ensure helper messages are English and errors actionable. Do not edit elective ranking or workbook layout; coordinate shared API changes with A, B, or C.

## Shared behavior that must not regress

1. **Course availability:** If a course is marked `Not offered` or its offering terms are missing, planner logic treats it provisionally as available in Semester A and Semester B, never Summer. This is a planning fallback only; do not alter the shared catalogue record to pretend that the course is offered. Show a clear warning. This rule applies throughout planning and elective eligibility.
2. **Incomplete course records:** Keep Degree Works candidates visible when catalogue lookup is unavailable, cancelled, or missing. Use a course-specific “Trying to get information for course …” message and the official catalogue URL when available. Do not discard the requirement or silently claim unknown title, credits, prerequisites, terms, or assessment are verified.
3. **Choice validation:** Reject duplicate and out-of-range selections; enforce exact requested count/credits. If a Degree Works requirement needs credits from different areas, enforce the minimum per distinct area and explain the actual reason when rejecting a selection.
4. **Prerequisites:** Distinguish satisfied, in-progress/scheduled, planned, missing, and unknown. Never show a missing-prerequisite warning for a prerequisite already completed, currently in progress, or planned in the proposed degree plan. In-progress courses must have blank workbook remarks.
5. **Workbook:** Keep year and semester borders visible around both headers and course cells. Preserve a clear distinction between known facts, assumptions, warnings, and unknown details.
6. **Dynamic lookup:** Get consent before requests, honor configured pacing and cooldowns, stop on blocks/challenges, and keep an offline/static path working.
7. **Course update scan:** Only refresh codes collected from the parsed Degree Works audit, and write them to the separate scanned database. Planning reads scanned records first and falls back field-by-field to the old database only where scanned information is absent. Show an estimate and obtain consent before requests; save successes individually, stop on a block, report partial failures, and return to the same process for explicit continuation.
8. **Major portability:** Do not hard-code a single student's programme or course list into planner logic. Requirements, area mappings, terms, and course data must come from the audit/config/catalogue.

## Data safety, Git, and collaboration precautions

- Never commit credentials, session cookies/state, Degree Works page text/audits, student names/IDs, personal workbooks, API keys, auth tokens, local absolute paths, machine names, or generated runtime data.
- Keep personal files, generated workbooks, builds, caches, logs, and local databases ignored by Git. Inspect `git status --short` and `git diff --check` before handing work off.
- Do not add a real user's audit or course selections as a test fixture. Use fictional names and synthetic course codes.
- Do not overwrite or force-push shared branch history. Publish only after verifying the authenticated GitHub identity and destination repository with the owner.
- Avoid editing another role's owned files. If an interface must change, document the reason and exact API/data-contract change in the handoff.
- Keep changes surgical. Do not reformat or rename unrelated modules as part of a feature fix.

## Handoff checklist

Every contributor should report:

1. What behavior changed and why.
2. The exact files changed, including any shared contract changes.
3. Tests/build commands run and their outcomes; mention tests not run.
4. Any assumptions, remaining known issues, or required follow-up by another role.
5. Confirmation that no private data, credentials, local paths, or generated artifacts were added.
