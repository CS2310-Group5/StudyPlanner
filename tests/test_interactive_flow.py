import json
import http.server
import os
import re
import select
import subprocess
import sys
import threading
import time
from pathlib import Path

from openpyxl import load_workbook


def user_data_directory(root):
    if sys.platform == "win32":
        return root / "CityUStudyPlan", {"LOCALAPPDATA": str(root)}
    if sys.platform == "darwin":
        return (
            root / "Library" / "Application Support" / "CityUStudyPlan",
            {"HOME": str(root)},
        )
    return root / "cityu-study-plan", {"XDG_DATA_HOME": str(root)}


def prepare_user_data(data_directory, degree_works_text):
    data_directory.mkdir(parents=True, exist_ok=True)
    (data_directory / "session_state.json").write_text("{}", encoding="utf-8")
    (data_directory / "page_text.txt").write_text(
        degree_works_text, encoding="utf-8"
    )


class CatalogueFixtureHandler(http.server.BaseHTTPRequestHandler):
    requests = []

    def do_GET(self):
        type(self).requests.append(self.path)
        if self.path.startswith("/AA9003"):
            self.send_response(403)
            self.send_header("Content-Length", "13")
            self.end_headers()
            self.wfile.write(b"Access Denied")
            return
        if self.path.startswith("/AA9002"):
            body = (
                '<div id="div_course_code_and_title">AA9002 - Incomplete Page</div>'
            ).encode("utf-8")
        else:
            body = (
                '<div id="div_course_code_and_title">AA9001 - Scan Test Course</div>'
                '<div id="div_course_credits">3</div>'
                '<div id="div_course_offering_term">Semester A</div>'
                '<div id="div_prerequisites">None</div>'
                '<div id="div_assessment_exam_pct">50%</div>'
                '<div id="div_assessment_other_pct">50%</div>'
            ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format, *_args):
        pass


def run(executable, data_root, input_text, args=()):
    data_directory, overrides = user_data_directory(data_root)
    environment = os.environ.copy()
    environment.update(overrides)
    return subprocess.run(
        [str(executable), *args],
        input=input_text,
        capture_output=True,
        text=True,
        env=environment,
        check=False,
    ), data_directory


def course_block(sheet, code):
    for row in sheet.iter_rows(values_only=True):
        if any(value == code for value in row):
            return " — ".join(str(value) for value in row if value not in (None, ""))
    return None


def course_row(sheet, code):
    for row in range(1, sheet.max_row + 1):
        if any(sheet.cell(row, column).value == code for column in range(1, sheet.max_column + 1)):
            return row
    return None


def course_remarks(sheet, code):
    for row in range(1, sheet.max_row + 1):
        for base in (1, 5, 9):
            if sheet.cell(row, base).value == code:
                return sheet.cell(row, base + 3).value
    return None


def year_block(sheet, year):
    for row in range(1, sheet.max_row + 1):
        if sheet.cell(row, 1).value == f"Year {year}":
            return {
                "year": row,
                "headings": row + 1,
                "columns": row + 2,
                "courses": row + 3,
            }
    return None


def run_area_choice(executable, data_root, area_data_path):
    data_directory, overrides = user_data_directory(data_root)
    environment = os.environ.copy()
    environment.update(overrides)
    process = subprocess.Popen(
        [str(executable)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=environment,
        bufsize=1,
    )
    output = []
    all_output = ""

    def send(value):
        process.stdin.write(value + "\n")
        process.stdin.flush()

    area_mapping = {}
    for area, codes in json.loads(area_data_path.read_text(encoding="utf-8")).items():
        for code in codes:
            area_mapping[code] = area
    sent_prompts = set()
    sent_pages = 0
    sent_selection = False
    sent_bad_selection = False
    selection_prompts = 0
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        ready, _, _ = select.select([process.stdout], [], [], 1)
        if not ready:
            continue
        chunk = os.read(process.stdout.fileno(), 4096).decode(
            "utf-8", errors="replace"
        )
        if not chunk:
            break
        output.append(chunk)
        all_output += chunk
        for marker, answer in (
            ("Enter 1 to start planning or 2 to scan course updates", "1"),
            ("Entry type:", "1"),
            ("Current semester:", "1"),
        ):
            if marker in all_output and marker not in sent_prompts:
                send(answer)
                sent_prompts.add(marker)
        preference_count = all_output.count("Your selection")
        if "Topics you do not prefer" in all_output and preference_count >= 1 and \
           "topics" not in sent_prompts:
            send("O")
            sent_prompts.add("topics")
        if "Assessment types you do not prefer" in all_output and preference_count >= 2 and \
           "assessment" not in sent_prompts:
            send("D")
            sent_prompts.add("assessment")
        if "Change any selected electives (" in all_output and \
           "workload-review" not in sent_prompts:
            send("N")
            sent_prompts.add("workload-review")

        rows_by_area = {}
        for match in re.finditer(r"(?m)^\s*(\d+)[.)]\s+(GE\d{4})\b", all_output):
            code = match[2]
            if code in area_mapping:
                rows_by_area.setdefault(area_mapping[code], []).append(int(match[1]))
        current_selection_prompts = all_output.lower().count(
            "enter the numbers above, separated by commas"
        )
        while current_selection_prompts > selection_prompts and not sent_selection:
            if "Area 2" not in rows_by_area or "Area 3" not in rows_by_area:
                process.kill()
                process.wait(timeout=5)
                raise AssertionError(
                    f"Verified candidates from two areas were not listed:\n{''.join(output)}"
                )
            if not sent_bad_selection and len(rows_by_area["Area 2"]) >= 2:
                send(
                    f"{rows_by_area['Area 2'][0]},{rows_by_area['Area 2'][1]}"
                )
                sent_bad_selection = True
            else:
                send(f"{rows_by_area['Area 2'][0]},{rows_by_area['Area 3'][0]}")
                sent_selection = True
            selection_prompts += 1
        pagination_count = all_output.count("Type M for the next page")
        if pagination_count > sent_pages and not sent_selection:
            if "Area 2" not in rows_by_area or "Area 3" not in rows_by_area:
                send("M")
            else:
                send("")
            sent_pages = pagination_count
        if process.poll() is not None:
            break
    process.stdin.close()
    return_code = process.wait(timeout=5)
    return subprocess.CompletedProcess(
        process.args, return_code, all_output, ""
    ), data_directory


def main():
    executable = Path(sys.argv[1]).resolve()
    data_root = Path(sys.argv[2]).resolve()
    catalogue = json.loads(Path(sys.argv[3]).read_text(encoding="utf-8"))
    degree_metadata = (
        "Demo Student\n"
        "Name\n"
        "00000000\n"
        "Student ID\n"
        "BSc Sample\n"
        "Programme\n"
        "College Sample CollegeLevel Bachelor's DegreeDegree BSc1 - Bachelor of Science"
        "Major SampleCohort 2026Department Computer ScienceMax Credits Permitted 120"
        "Expected Graduation Date 01-Jun-2029\n"
        "Audit Date 21/11/2026\n"
    )

    success_root = data_root / "successful-run"
    success_directory, _ = user_data_directory(success_root)
    prepare_user_data(
        success_directory,
        "Demo Student\n"
        "Name\n"
        "00000000\n"
        "Student ID\n"
        "BSc Sample\n"
        "Programme\n"
        "College Sample CollegeLevel Bachelor's DegreeDegree BSc1 - Bachelor of Science"
        "Major SampleCohort 2026Department Computer ScienceMax Credits Permitted 120"
        "Expected Graduation Date 01-Jun-2029\n"
        "Audit Date 21/11/2026\n"
        "Still Needed:\n"
        "CS2310\n"
        "3 credits in AC3202 or BMS1901 or CS1102\n",
    )
    cache_directory = success_directory / ".cityu_cache"
    cache_directory.mkdir(parents=True, exist_ok=True)
    (cache_directory / "cached-page.html").write_text("cached", encoding="utf-8")
    (cache_directory / "catalogue-cooldown-until").write_text(
        f"{int(time.time()) + 3600}\n", encoding="utf-8"
    )
    log_directory = success_directory / ".cityu_logs"
    log_directory.mkdir()
    (log_directory / "run.log").write_text("test", encoding="utf-8")
    (success_directory / "overrides.json").write_text("test", encoding="utf-8")
    result, success_directory = run(
        executable, success_root,
        "CLEAN\ny\n1\nab\na,b\nP\nO\nA,D\nC\n999\n1\n",
    )
    if result.returncode != 0:
        raise AssertionError(
            f"Interactive flow failed ({result.returncode}):\n"
            f"{result.stdout}\n{result.stderr}"
        )
    prompts = [
        "Enter 1 to start planning or 2 to scan course updates",
        "Topics you do not prefer",
        "Assessment types you do not prefer",
        "enter the numbers above, separated by commas",
    ]
    positions = [result.stdout.find(prompt) for prompt in prompts]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        raise AssertionError(f"Unexpected prompt order:\n{result.stdout}")
    if "100% continuous assessment" not in result.stdout:
        raise AssertionError("Assessment preference wording is missing")
    if "Choice data check" in result.stdout:
        raise AssertionError("Complete catalogue data was incorrectly rejected")
    if "https://www.cityu.edu.hk/catalogue/ug/current/course/" not in result.stdout:
        raise AssertionError("Ranked elective candidates should include official course URLs")
    if "Enter at least one valid option letter." not in result.stdout:
        raise AssertionError("Invalid preference input was not rejected")
    if '"None / all fine" cannot be combined with another choice.' not in result.stdout:
        raise AssertionError("Conflicting assessment preferences were not rejected")
    if "not shown above" not in result.stdout:
        raise AssertionError("Invalid elective rows were not rejected")
    if "[OK proceed" in result.stdout or "Received:" in result.stdout:
        raise AssertionError("Redundant global prompt hints or input echoes were printed")
    if not (success_directory / "session_state.json").is_file() or not (
        success_directory / "page_text.txt"
    ).is_file():
        raise AssertionError("Selecting option C deleted private session data")
    if (success_directory / ".cityu_cache" / "cached-page.html").exists():
        raise AssertionError("CLEAN did not remove the cached page")
    if not (
        success_directory / ".cityu_cache" / "catalogue-cooldown-until"
    ).is_file():
        raise AssertionError("CLEAN removed the catalogue cooldown")
    if (success_directory / ".cityu_logs").exists() or (
        success_directory / "overrides.json"
    ).exists():
        raise AssertionError("CLEAN did not remove logs and URL overrides")

    output_match = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", result.stdout
    )
    if not output_match:
        raise AssertionError(f"No workbook path was printed:\n{result.stdout}")
    workbook_path = Path(output_match.group(1))
    if not workbook_path.is_file():
        raise AssertionError(f"Workbook was not created: {workbook_path}")
    if workbook_path.parent != success_directory / "Study Plans":
        raise AssertionError(f"Workbook was saved to an unexpected folder: {workbook_path}")

    workbook = load_workbook(workbook_path, data_only=True)
    try:
        schedule = workbook["Study Schedule"]
        for year in range(1, 4):
            block = year_block(schedule, year)
            if not block:
                raise AssertionError(f"Year {year} was not separated vertically")
            if [schedule.cell(block["headings"], column).value for column in (1, 5, 9)] != [
                "Sem A", "Sem B", "Summer"
            ]:
                raise AssertionError(f"Year {year} semester boxes are not horizontal")
            if [
                schedule.cell(block["columns"], column).value
                for column in (1, 2, 3, 4)
            ] != ["Code", "Course", "Credits", "Remarks"]:
                raise AssertionError("Course details should use concise separate columns")
            year_row = block["year"]
            if schedule.cell(year_row, 1).border.top.style != "medium":
                raise AssertionError("Year box should have a clear outer top border")
            if schedule.cell(block["headings"], 1).border.left.style != "medium" or \
               schedule.cell(block["headings"], 4).border.right.style != "medium" or \
               schedule.cell(block["headings"], 5).border.left.style != "medium":
                raise AssertionError("Semester boxes and separators need clear borders")
        if schedule["B3"].value != "Demo Student" or schedule["B4"].value != "00000000":
            raise AssertionError("Degree Works identity fields were not added to the workbook")
        if schedule["B13"].value != "Semester A" or schedule["B14"].value != "3 years":
            raise AssertionError("The workbook did not use the parsed current term and duration")
        required_block = course_block(schedule, "CS2310")
        if not required_block:
            raise AssertionError("Required Degree Works course is missing from the workbook")
        summary_row = next(
            row for row in range(1, schedule.max_row + 1)
            if schedule.cell(row, 1).value == "Year 1 total: "
            or (isinstance(schedule.cell(row, 1).value, str) and
                schedule.cell(row, 1).value.startswith("Year 1 total:"))
        )
        if schedule.cell(summary_row, 1).border.bottom.style != "medium" or \
           schedule.cell(summary_row, 12).border.bottom.style != "medium":
            raise AssertionError("Year box should have a clear outer bottom border")
        selected_block = next(
            (course_block(schedule, code) or course_block(
                workbook["Manual Placement"], code
            ) for code in ("AC3202", "BMS1901", "CS1102")
             if course_block(schedule, code) or course_block(
                 workbook["Manual Placement"], code
             )),
            None,
        )
        if not selected_block or "elective" not in selected_block.lower():
            raise AssertionError("The chosen elective is not marked in the schedule")
        elective_rows = list(
            workbook["Elective Selections"].iter_rows(min_row=3, values_only=True)
        )
        selected_row = next(
            (row for row in elective_rows if row and row[2] in selected_block),
            None,
        )
        if not selected_row:
            raise AssertionError("The Elective Selections sheet does not match the schedule")
        if not selected_row[5].startswith(
            "https://www.cityu.edu.hk/catalogue/ug/current/course/"
        ):
            raise AssertionError("The elective selection sheet is missing the course URL")
    finally:
        workbook.close()

    prerequisite_root = data_root / "prerequisite-statuses"
    prerequisite_directory, _ = user_data_directory(prerequisite_root)
    prepare_user_data(
        prerequisite_directory,
        "Completed Courses:\n"
        "CS1111 Completed Sample Course\n"
        "In-progress\n"
        "Course\tTitle\tGrade\tCredits\tTerm\n"
        "CS1112\tCurrent Sample Course\tIP\t3\tSemester A 2026/27\n"
        "Still Needed:\n"
        "CS1113\n"
        "6 credits in ZZ2001 or ZZ2002\n"
        "Expected Graduation Date 01-Jun-2029\n"
        "Audit Date 21/11/2026\n",
    )
    prerequisite_catalogue = {
        "CS1112": {
            "title": "Current Sample Course",
            "credits": 3,
            "semesters": ["Semester A", "Semester B"],
            "prerequisites": [],
            "prerequisites_text": "None",
            "catalogue_detail_available": True,
            "url": "",
        },
        "CS1113": {
            "title": "Planned Sample Course",
            "credits": 3,
            "semesters": ["Semester A", "Semester B"],
            "prerequisites": [],
            "prerequisites_text": "None",
            "catalogue_detail_available": True,
            "url": "",
        },
        "ZZ2001": {
            "title": "Prerequisite Status Sample",
            "credits": 3,
            "semesters": ["Semester A", "Semester B"],
            "prerequisites": ["CS1111", "CS1112", "CS1113", "CS9999"],
            "prerequisites_text": "CS1111, CS1112, CS1113, CS9999",
            "catalogue_detail_available": True,
            "url": "",
        },
        "ZZ2002": {
            "title": "No Prerequisite Sample",
            "credits": 3,
            "semesters": ["Semester A", "Semester B"],
            "prerequisites": [],
            "prerequisites_text": "None",
            "catalogue_detail_available": True,
            "url": "",
        },
    }
    (prerequisite_directory / "courses_db.json").write_text(
        json.dumps(prerequisite_catalogue), encoding="utf-8"
    )
    prerequisite_result, _ = run(
        executable, prerequisite_root, "1\nO\nD\n1,2\n"
    )
    if prerequisite_result.returncode != 0:
        raise AssertionError(
            "Prerequisite status flow failed:\n"
            f"{prerequisite_result.stdout}\n{prerequisite_result.stderr}"
        )
    for status in (
        "CS1111 ✅ studied",
        "CS1112 🔄 studying",
        "CS1113 📅 planned",
        "CS9999 ⚠️ not listed",
        "Prerequisites: None",
    ):
        if status not in prerequisite_result.stdout:
            raise AssertionError(
                f"Elective prerequisite status {status!r} was not shown:\n"
                f"{prerequisite_result.stdout}"
            )
    prerequisite_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)",
        prerequisite_result.stdout,
    )
    if not prerequisite_output:
        raise AssertionError("Prerequisite-status run did not create its workbook")
    prerequisite_workbook = load_workbook(
        prerequisite_output.group(1), data_only=True
    )
    try:
        schedule = prerequisite_workbook["Study Schedule"]
        missing_remarks = course_remarks(schedule, "ZZ2001") or ""
        if "Missing CS9999" not in missing_remarks:
            raise AssertionError(
                f"Missing prerequisite codes were not identified: {missing_remarks}"
            )
        if any(code in missing_remarks for code in ("CS1111", "CS1112", "CS1113")):
            raise AssertionError(
                f"Studied, studying, or planned prerequisites were warned as missing: {missing_remarks}"
            )
        if course_remarks(schedule, "CS1112") not in (None, ""):
            raise AssertionError("An in-progress course should have blank remarks")
    finally:
        prerequisite_workbook.close()

    balance_root = data_root / "elective-workload-review"
    balance_directory, _ = user_data_directory(balance_root)
    balance_report = (
        "Expected Graduation Date 01-Jun-2028\n"
        "Audit Date 21/11/2026\n"
        "Still Needed:\n"
        "CC1001\n"
        "CC1002\n"
        "CC1003\n"
        "CC1004\n"
        "CC1005\n"
        "6 credits in EA1001 or EA1002 or EB1001 or EB1002\n"
    )
    prepare_user_data(balance_directory, balance_report)
    balance_catalogue = {}
    for index in range(1, 6):
        balance_catalogue[f"CC100{index}"] = {
            "title": f"Required Sample Course {index}",
            "credits": 3,
            "semesters": ["Semester A"],
            "prerequisites": [],
            "prerequisites_text": "None",
            "catalogue_detail_available": True,
            "url": "",
        }
    for code, semester in (
        ("EA1001", "Semester A"),
        ("EA1002", "Semester A"),
        ("EB1001", "Semester B"),
        ("EB1002", "Semester B"),
    ):
        balance_catalogue[code] = {
            "title": f"Elective {code}",
            "credits": 3,
            "semesters": [semester],
            "prerequisites": [],
            "prerequisites_text": "None",
            "catalogue_detail_available": True,
            "url": "",
        }
    (balance_directory / "courses_db.json").write_text(
        json.dumps(balance_catalogue), encoding="utf-8"
    )
    balance_result, _ = run(
        executable, balance_root, "1\nO\nD\n1,2\nY\n3,4\n"
    )
    if balance_result.returncode != 0:
        raise AssertionError(
            "Elective workload review flow failed:\n"
            f"{balance_result.stdout}\n{balance_result.stderr}"
        )
    if "Elective workload review" not in balance_result.stdout or \
       "Change any selected electives" not in balance_result.stdout or \
       "EB1001" not in balance_result.stdout:
        raise AssertionError("The overload review did not offer alternate-semester electives")
    balance_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", balance_result.stdout
    )
    if not balance_output:
        raise AssertionError("The revised workload plan did not produce a workbook")
    balance_workbook = load_workbook(balance_output.group(1), data_only=True)
    try:
        selections = list(
            balance_workbook["Elective Selections"].iter_rows(
                min_row=3, values_only=True
            )
        )
        selected_codes = [row[2] for row in selections if row and row[2]]
        if selected_codes != ["EB1001", "EB1002"]:
            raise AssertionError(f"Alternate-term electives were not saved: {selected_codes}")
    finally:
        balance_workbook.close()

    placement_root = data_root / "course-placement-rules"
    placement_directory, _ = user_data_directory(placement_root)
    prepare_user_data(
        placement_directory,
        "Demo Student\n"
        "Name\n"
        "00000000\n"
        "Student ID\n"
        "BSc Sample\n"
        "Programme\n"
        "College Sample CollegeLevel Bachelor's DegreeDegree BSc1 - Bachelor of Science"
        "Major SampleCohort 2026Department Computer ScienceMax Credits Permitted 120"
        "Expected Graduation Date 01-Jun-2029\n"
        "Audit Date 21/11/2026\n"
        "Still Needed:\n"
        "PROJ9001\n"
        "INTR9001\n"
        "MISS9001\n"
        "NOIN9001\n"
        "3 credits in UE9001 or UE9002\n"
        "In-progress\n"
        "Course\tTitle\tGrade\tCredits\tTerm\n"
        "IP9001\tCurrent Sample Course A\tIP\t3\tSem B 2026/27\n"
        "IP9002\tCurrent Sample Course B\tIP\t3\tSummer 2027\n"
        "Legend\n",
    )
    placement_catalogue = {
        "IP9001": {
            "title": "Current Sample Course A",
            "credits": 3,
            "semesters": ["Semester B"],
            "prerequisites": [],
            "catalogue_detail_available": True,
            "url": "",
        },
        "IP9002": {
            "title": "Current Sample Course B",
            "credits": 3,
            "semesters": ["Summer"],
            "prerequisites": [],
            "catalogue_detail_available": True,
            "url": "",
        },
        "PROJ9001": {
            "title": "Final Year Project",
            "credits": 3,
            "semesters": [],
            "prerequisites": [],
            "catalogue_detail_available": True,
            "url": "",
        },
        "INTR9001": {
            "title": "Professional Internship",
            "credits": 3,
            "semesters": ["Semester A", "Semester B"],
            "prerequisites": [],
            "catalogue_detail_available": True,
            "url": "",
        },
        "MISS9001": {
            "title": "Course With Missing Offering",
            "credits": 3,
            "semesters": [],
            "prerequisites": [],
            "catalogue_detail_available": True,
            "url": "",
        },
        "NOIN9001": {
            "title": "",
            "prerequisites": ["UNKNOWN"],
            "catalogue_detail_available": False,
            "url": "",
        },
    }
    (placement_directory / "courses_db.json").write_text(
        json.dumps(placement_catalogue), encoding="utf-8"
    )
    cooldown_directory = placement_directory / ".cityu_cache"
    cooldown_directory.mkdir(exist_ok=True)
    (cooldown_directory / "catalogue-cooldown-until").write_text(
        f"{int(time.time()) + 3600}\n", encoding="utf-8"
    )
    placement, placement_directory = run(
        executable, placement_root, "1\nO\nD\n1\n"
    )
    if placement.returncode != 0:
        raise AssertionError(
            f"Missing-data placement flow failed ({placement.returncode}):\n"
            f"{placement.stdout}\n{placement.stderr}"
        )
    placement_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", placement.stdout
    )
    if not placement_output:
        raise AssertionError("The missing-data scenario did not produce an Excel workbook")
    placement_workbook = load_workbook(placement_output.group(1), data_only=True)
    try:
        schedule = placement_workbook["Study Schedule"]
        year_one = year_block(schedule, 1)
        year_three = year_block(schedule, 3)
        if not year_one or not year_three:
            raise AssertionError("The three-year Degree Works horizon was not preserved")
        if not course_block(schedule, "IP9001") or \
           "Current Sample Course A" not in course_block(schedule, "IP9001") or \
           not course_block(schedule, "IP9002") or \
           "Current Sample Course B" not in course_block(schedule, "IP9002"):
            raise AssertionError("Degree Works In-progress courses must be placed directly in Year 1 Sem A")
        project_row = next(
            row for row in range(1, schedule.max_row + 1)
            if any(value == "PROJ9001" for value in [
                schedule.cell(row, column).value for column in range(1, 13)
            ])
        )
        if not year_three["year"] < project_row < year_three["year"] + 14:
            raise AssertionError("The Project course was not scheduled in the final year")
        internship_block = course_block(
            placement_workbook["Manual Placement"], "INTR9001"
        )
        if not internship_block:
            raise AssertionError("The Internship course was not retained for manual Summer placement")
        summer_cells = [
            schedule.cell(row, column).value
            for row in range(1, schedule.max_row + 1)
            for column in (9, 10, 11, 12)
        ]
        if "INTR9001" in summer_cells:
            raise AssertionError("Summer boxes should remain unplanned for manual entry")
        assumed_course = course_block(schedule, "MISS9001")
        if not assumed_course or "Confirm offering" not in assumed_course:
            raise AssertionError("Missing offering terms should use the Semester A/B assumption")
        if "PLANNING WARNINGS" not in str(schedule["A20"].value):
            raise AssertionError("Missing catalogue information should trigger a visible workbook warning")
        no_information = course_block(schedule, "NOIN9001")
        if not no_information or "3 credits assumed" not in no_information:
            raise AssertionError("Missing title/credit details should remain visible without blocking export")
        chosen_missing_data = list(
            placement_workbook["Elective Selections"].iter_rows(
                min_row=3, values_only=True
            )
        )
        if not any(row[2] == "UE9001" and row[4] == "3*" for row in chosen_missing_data if row):
            raise AssertionError("Missing elective credits should default to 3 and remain selectable")
    finally:
        placement_workbook.close()

    cancelled_root = data_root / "lookup-cancelled"
    cancelled_directory, _ = user_data_directory(cancelled_root)
    prepare_user_data(
        cancelled_directory,
        "Demo Student\n"
        "Name\n"
        "00000000\n"
        "Student ID\n"
        "BSc Sample\n"
        "Programme\n"
        "College Sample CollegeLevel Bachelor's DegreeDegree BSc1 - Bachelor of Science"
        "Major SampleCohort 2026Department Computer ScienceMax Credits Permitted 120"
        "Expected Graduation Date 01-Jun-2029\n"
        "Audit Date 21/11/2026\n"
        "Still Needed:\n"
        "CA9001\n",
    )
    cancelled, cancelled_directory = run(executable, cancelled_root, "1\nno\n")
    if cancelled.returncode != 0:
        raise AssertionError(
            f"Declining lookups should still produce a provisional workbook:\n"
            f"{cancelled.stdout}\n{cancelled.stderr}"
        )
    cancelled_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", cancelled.stdout
    )
    if not cancelled_output:
        raise AssertionError("Declining lookups did not produce the final workbook")
    if "Course lookups were cancelled" not in cancelled.stdout:
        raise AssertionError("A cancelled lookup should be reported without aborting the plan")
    cancelled_workbook = load_workbook(cancelled_output.group(1), data_only=True)
    try:
        cancelled_schedule = cancelled_workbook["Study Schedule"]
        cancelled_year_one = year_block(cancelled_schedule, 1)
        if cancelled_schedule.cell(cancelled_year_one["courses"], 2).value != "No courses planned":
            raise AssertionError("Year 1 Sem A must remain reserved for Degree Works In-progress courses")
        cancelled_course = course_block(
            cancelled_schedule, "CA9001"
        )
        if not cancelled_course or "3 credits assumed" not in cancelled_course:
            raise AssertionError("The workbook should identify unresolved catalogue details")
    finally:
        cancelled_workbook.close()

    offering_root = data_root / "offering-terms-only-missing"
    offering_directory, _ = user_data_directory(offering_root)
    prepare_user_data(
        offering_directory,
        "Demo Student\n"
        "Name\n"
        "00000000\n"
        "Student ID\n"
        "BSc Sample\n"
        "Programme\n"
        "College Sample CollegeLevel Bachelor's DegreeDegree BSc1 - Bachelor of Science"
        "Major SampleCohort 2026Department Computer ScienceMax Credits Permitted 120"
        "Expected Graduation Date 01-Jun-2029\n"
        "Audit Date 21/11/2026\n"
        "Still Needed:\n"
        "MISS9001\n",
    )
    (offering_directory / "courses_db.json").write_text(
        json.dumps(
            {
                "MISS9001": {
                    "title": "Offering Terms Not Listed",
                    "credits": 3,
                    "semesters": [],
                    "prerequisites": [],
                    "catalogue_detail_available": True,
                    "url": "",
                }
            }
        ),
        encoding="utf-8",
    )
    offering_result, _ = run(executable, offering_root, "1\n")
    if offering_result.returncode != 0:
        raise AssertionError(
            f"Missing offering terms should use the Semester A/B assumption:\n"
            f"{offering_result.stdout}\n{offering_result.stderr}"
        )
    if "Type OK to begin these lookups" in offering_result.stdout:
        raise AssertionError("A known course missing only its offering terms should not block workbook generation")
    offering_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", offering_result.stdout
    )
    if not offering_output:
        raise AssertionError("A course with missing offering terms did not produce a workbook")
    offering_workbook = load_workbook(offering_output.group(1), data_only=True)
    try:
        offering_course = course_block(
            offering_workbook["Study Schedule"], "MISS9001"
        )
        if not offering_course or "Confirm offering" not in offering_course:
            raise AssertionError("The assumption was not recorded in the workbook")
    finally:
        offering_workbook.close()

    paginated_candidates = [f"ZZ{9000 + number}" for number in range(1, 22)]
    pagination_root = data_root / "candidate-pagination"
    pagination_directory, _ = user_data_directory(pagination_root)
    prepare_user_data(
        pagination_directory,
        "Still Needed:\n3 credits in " + " or ".join(paginated_candidates) + "\n",
    )
    pagination_catalogue = {
        code: {
            "title": f"Test elective {index}",
            "credits": 3,
            "semesters": ["Semester A"],
            "prerequisites": [],
            "prerequisites_text": "",
            "exclusive_courses": [],
            "catalogue_notes": "",
            "catalogue_year": "2026/27",
            "catalogue_detail_available": True,
            "area": "",
            "tags": [],
            "url": "",
            "exam_percent": 0,
            "other_percent": 100,
            "raw_items": ["Continuous Assessment: 100%"],
            "raw_text": "",
            "assessment_unknown": True,
        }
        for index, code in enumerate(paginated_candidates, start=1)
    }
    (pagination_directory / "courses_db.json").write_text(
        json.dumps(pagination_catalogue), encoding="utf-8"
    )
    pagination, _ = run(
        executable, pagination_root, "1\n1\n1\nO\nD\n21\n"
    )
    if pagination.returncode != 0:
        raise AssertionError(
            f"Candidate pagination flow failed ({pagination.returncode}):\n"
            f"{pagination.stdout}\n{pagination.stderr}"
        )
    if "  21)" not in pagination.stdout:
        raise AssertionError(f"Equal-score tie was split or row 21 was hidden:\n{pagination.stdout}")
    if "Type M for the next page" in pagination.stdout:
        raise AssertionError("Equal-score candidates must not be split across pages")
    pagination_path = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", pagination.stdout
    )
    if not pagination_path:
        raise AssertionError(f"Paginated plan did not produce an output path:\n{pagination.stdout}")
    paginated_workbook = load_workbook(pagination_path.group(1), data_only=True)
    try:
        selected_rows = list(
            paginated_workbook["Elective Selections"].iter_rows(
                min_row=3, values_only=True
            )
        )
        if not any(row and row[2] in paginated_candidates for row in selected_rows):
            raise AssertionError("The paginated candidate selection was not saved")
    finally:
        paginated_workbook.close()

    provisional_root = data_root / "provisional-not-offered-choice"
    provisional_directory, _ = user_data_directory(provisional_root)
    provisional_candidates = [f"ZX{9000 + number}" for number in range(1, 19)]
    prepare_user_data(
        provisional_directory,
        "Still Needed:\n6 credits in " +
        " or ".join(provisional_candidates) + "\n",
    )
    provisional_catalogue = {
        provisional_candidates[0]: {
            "title": "Temporarily Not Offered",
            "credits": 3,
            "semesters": ["Not offered"],
            "prerequisites": [],
            "catalogue_detail_available": True,
            "url": "",
        }
    }
    (provisional_directory / "courses_db.json").write_text(
        json.dumps(provisional_catalogue), encoding="utf-8"
    )
    provisional, _ = run(
        executable,
        provisional_root,
        "1\nN\n1\n1\nO\nD\n1,2\n",
    )
    if provisional.returncode != 0:
        raise AssertionError(
            "Provisional offering and missing-course flow failed:\n"
            f"{provisional.stdout}\n{provisional.stderr}"
        )
    for code in provisional_candidates:
        if code not in provisional.stdout:
            raise AssertionError(f"Degree Works candidate {code} was hidden")
    if "provisionally treated as Semester A/B" not in provisional.stdout:
        raise AssertionError("Not offered was not provisionally included in Semester A/B")
    if f"Trying to get information for course {provisional_candidates[1]}" not in provisional.stdout:
        raise AssertionError("Missing course information was not described clearly")
    if "✅ Selected: ZX9001 ZX9002" not in provisional.stdout:
        raise AssertionError(
            f"Two 3-credit courses did not satisfy the 6-credit choice:\n{provisional.stdout}"
        )

    ge1401_root = data_root / "verified-ge1401-run"
    ge1401_directory, _ = user_data_directory(ge1401_root)
    prepare_user_data(ge1401_directory, "Still Needed:\n3 credits in GE1401\n")
    ge1401_result, ge1401_directory = run(
        executable, ge1401_root, "1\n1\n1\nO\nD\n1\n"
    )
    if ge1401_result.returncode != 0:
        raise AssertionError(
            f"Verified GE1401 choice flow failed ({ge1401_result.returncode}):\n"
            f"{ge1401_result.stdout}\n{ge1401_result.stderr}"
        )
    if "No online catalogue requests are pending." not in ge1401_result.stdout:
        raise AssertionError("Bundled GE1401 data should avoid an unnecessary network request")
    ge1401_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", ge1401_result.stdout
    )
    if not ge1401_output:
        raise AssertionError(f"GE1401 plan did not produce an output path:\n{ge1401_result.stdout}")
    ge1401_workbook = load_workbook(ge1401_output.group(1), data_only=True)
    try:
        ge1401_rows = list(
            ge1401_workbook["Study Schedule"].iter_rows(values_only=True)
        )
        selected_ge1401 = course_block(ge1401_workbook["Study Schedule"], "GE1401")
        if not selected_ge1401 or "3" not in selected_ge1401:
            raise AssertionError("GE1401 was not selected with its verified 3 credits")
    finally:
        ge1401_workbook.close()

    ge2410_root = data_root / "verified-ge2410-run"
    ge2410_directory, _ = user_data_directory(ge2410_root)
    prepare_user_data(ge2410_directory, "Still Needed:\n3 credits in GE2410\n")
    ge2410_result, _ = run(
        executable, ge2410_root, "1\n1\n1\nO\nD\n1\n"
    )
    if ge2410_result.returncode != 0:
        raise AssertionError(
            f"Verified GE2410 choice flow failed ({ge2410_result.returncode}):\n"
            f"{ge2410_result.stdout}\n{ge2410_result.stderr}"
        )
    ge2410_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", ge2410_result.stdout
    )
    if not ge2410_output:
        raise AssertionError(f"GE2410 plan did not produce an output path:\n{ge2410_result.stdout}")
    if "No online catalogue requests are pending." not in ge2410_result.stdout:
        raise AssertionError("Bundled GE2410 data should avoid an unnecessary lookup")
    if "Topics you do not prefer" in ge2410_result.stdout:
        raise AssertionError("A single required course should not trigger preference questions")
    for expected in ("Study plan saved to:",):
        if expected not in ge2410_result.stdout:
            raise AssertionError(f"GE2410 CLI output is missing {expected!r}:\n{ge2410_result.stdout}")
    if "Score " in ge2410_result.stdout or "Catalogue note:" in ge2410_result.stdout:
        raise AssertionError("Candidate details include non-essential implementation data")
    ge2410_workbook = load_workbook(ge2410_output.group(1), data_only=True)
    try:
        ge2410_rows = list(
            ge2410_workbook["Study Schedule"].iter_rows(values_only=True)
        )
        selected_ge2410 = course_block(ge2410_workbook["Study Schedule"], "GE2410")
        if not selected_ge2410 or "3" not in selected_ge2410:
            raise AssertionError("GE2410 was not selected with its verified 3 credits")
    finally:
        ge2410_workbook.close()

    ge1501_root = data_root / "verified-ge1501-run"
    ge1501_directory, _ = user_data_directory(ge1501_root)
    prepare_user_data(ge1501_directory, "Still Needed:\n3 credits in GE1501\n")
    ge1501_result, _ = run(
        executable, ge1501_root, "1\n1\n1\nO\nD\n1\n"
    )
    if ge1501_result.returncode != 0:
        raise AssertionError(
            f"Verified GE1501 choice flow failed ({ge1501_result.returncode}):\n"
            f"{ge1501_result.stdout}\n{ge1501_result.stderr}"
        )
    if "Topics you do not prefer" in ge1501_result.stdout:
        raise AssertionError("A single required course should not trigger preference questions")
    if "GE1501 — Chinese Civilisation - History and Philosophy — 3 credits (check prerequisites)" in ge1501_result.stdout:
        raise AssertionError("GE1501 should not be flagged for an unspecified prerequisite")
    ge1501_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", ge1501_result.stdout
    )
    if not ge1501_output:
        raise AssertionError("GE1501 was not placed in the generated workbook")
    ge1501_workbook = load_workbook(ge1501_output.group(1), data_only=True)
    try:
        if not course_block(ge1501_workbook["Study Schedule"], "GE1501"):
            raise AssertionError("GE1501 was missing from the generated schedule")
    finally:
        ge1501_workbook.close()

    area_root = data_root / "area-requirement"
    area_directory, _ = user_data_directory(area_root)
    prepare_user_data(
        area_directory,
        "Still Needed:\n"
        "6 Credit Units from Area 1 or 2 or 3\n"
        "Area 1: Minimum 3 Credit Units\n"
        "Area 2: Minimum 3 Credit Units\n"
        "Area 3: Minimum 3 Credit Units\n",
    )
    area_result, area_directory = run_area_choice(
        executable, area_root, Path(sys.argv[3]).with_name("ge_areas.json")
    )
    if area_result.returncode != 0:
        raise AssertionError(
            f"Mapped Degree Works area choices failed:\n"
            f"{area_result.stdout}\n{area_result.stderr}"
        )
    if "different listed areas" not in area_result.stdout:
        raise AssertionError("A same-area 6-credit choice was not explained clearly")
    for area in ("◆ Area 1", "◆ Area 2", "◆ Area 3"):
        if area not in area_result.stdout:
            raise AssertionError(f"Area choice was not grouped under {area}")
    if "does not identify which courses belong to those areas" in area_result.stdout:
        raise AssertionError("Official area mappings were not used")
    displayed_area_rows = re.findall(
        r"(?m)^\s*(\d+)[.)]\s+(GE\d{4})\b", area_result.stdout
    )
    displayed_numbers = [int(number) for number, _ in displayed_area_rows]
    if displayed_numbers != list(range(1, len(displayed_numbers) + 1)):
        raise AssertionError(
            "Area-grouped candidates must use consecutive numbers in displayed order"
        )
    area_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", area_result.stdout
    )
    if not area_output:
        raise AssertionError("The mapped area choice did not produce a workbook")
    area_workbook = load_workbook(area_output.group(1), data_only=True)
    try:
        area_selections = list(
            area_workbook["Elective Selections"].iter_rows(
                min_row=3, values_only=True
            )
        )
        selected_area_codes = [
            row[2] for row in area_selections if row and row[2]
        ]
        if len(selected_area_codes) != 2:
            raise AssertionError("The area requirement should select two courses totaling 6 credits")
        if len(set(selected_area_codes)) != 2:
            raise AssertionError("A course was repeated in the area requirement")
        if len({
            next(
                area
                for area, codes in json.loads(
                    Path(sys.argv[3]).with_name("ge_areas.json").read_text(
                        encoding="utf-8"
                    )
                ).items()
                if code in codes
            )
            for code in selected_area_codes
        }) != 2:
            raise AssertionError("The selection did not meet the minimum credits from two areas")
    finally:
        area_workbook.close()

    database_root = data_root / "database-remaining-only"
    user_data_directory(database_root)
    database_source = database_root / "catalogue-source.txt"
    database_root.mkdir(parents=True, exist_ok=True)
    database_source.write_text(
        "Undergraduate Catalogue 2026/27\n"
        "DB9901 - Complete test course\n"
        "DB9902 - Remaining test course\n"
        "COURSES >>>\n"
        "DB9901 - Complete test course\n"
        "Offering Academic Unit\n"
        "Test Department\n"
        "Credit Units\n"
        "3\n"
        "Course Offering Term\n"
        "Semester A\n"
        "Pre-requisite\n"
        "None\n"
        "Assessment\n"
        "Exam\n"
        "50%\n",
        encoding="utf-8",
    )
    database_result, _ = run(
        executable,
        database_root,
        "N\n",
        args=("--database", str(database_source)),
    )
    if database_result.returncode != 0:
        raise AssertionError(
            "Remaining-only database fill failed:\n"
            f"{database_result.stdout}\n{database_result.stderr}"
        )
    if "1 courses still need page details." not in database_result.stdout:
        raise AssertionError(
            "Database fill did not limit lookup to the one incomplete record:\n"
            f"{database_result.stdout}"
        )
    if "automatic lookup was not started" not in database_result.stdout:
        raise AssertionError("Database test should stop before making a web request")

    conflicting_term_root = data_root / "conflicting-in-progress-terms"
    conflicting_term_directory, _ = user_data_directory(conflicting_term_root)
    prepare_user_data(
        conflicting_term_directory,
        "Demo Student\n"
        "Name\n"
        "00000000\n"
        "Student ID\n"
        "BSc Sample\n"
        "Programme\n"
        "College Sample CollegeLevel Bachelor's DegreeDegree BSc1 - Bachelor of Science"
        "Major SampleCohort 2026Department Computer ScienceMax Credits Permitted 120"
        "Expected Graduation Date 01-Jun-2029\n"
        "Audit Date 21/11/2026\n"
        "In-progress\n"
        "Course\tTitle\tGrade\tCredits\tTerm\n"
        "ZZ1001\tCurrent course A\tIP\t3\tSem A 2026/27\n"
        "ZZ1002\tCurrent course B\tIP\t3\tSem B 2026/27\n"
        "Legend\n",
    )
    (conflicting_term_directory / "courses_db.json").write_text(
        json.dumps(
            {
                code: {
                    "title": f"Current course {suffix}",
                    "credits": 3,
                    "semesters": ["Semester A", "Semester B"],
                    "prerequisites": [],
                    "catalogue_detail_available": True,
                    "url": "",
                }
                for code, suffix in (("ZZ1001", "A"), ("ZZ1002", "B"))
            }
        ),
        encoding="utf-8",
    )
    conflicting_terms, _ = run(executable, conflicting_term_root, "1\n")
    if conflicting_terms.returncode != 0:
        raise AssertionError(
            f"Conflicting in-progress terms should still produce a workbook:\n"
            f"{conflicting_terms.stdout}\n{conflicting_terms.stderr}"
        )
    if "differing term labels" not in conflicting_terms.stdout:
        raise AssertionError("Conflicting current terms were not identified in a warning")
    conflicting_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", conflicting_terms.stdout
    )
    if not conflicting_output:
        raise AssertionError("Differing in-progress terms prevented workbook generation")
    conflicting_workbook = load_workbook(conflicting_output.group(1), data_only=True)
    try:
        year_one = year_block(conflicting_workbook["Study Schedule"], 1)
        if not course_block(conflicting_workbook["Study Schedule"], "ZZ1001") or \
           not course_block(conflicting_workbook["Study Schedule"], "ZZ1002"):
            raise AssertionError("All In-progress rows should be listed in Year 1 Sem A")
    finally:
        conflicting_workbook.close()

    english_root = data_root / "english-course-sequencing"
    english_directory, _ = user_data_directory(english_root)
    prepare_user_data(
        english_directory,
        "Demo Student\n"
        "Name\n"
        "00000000\n"
        "Student ID\n"
        "BSc Sample\n"
        "Programme\n"
        "College Sample CollegeLevel Bachelor's DegreeDegree BSc1 - Bachelor of Science"
        "Major SampleCohort 2026Department Computer ScienceMax Credits Permitted 120"
        "Expected Graduation Date 01-Jun-2029\n"
        "Audit Date 21/11/2026\n"
        "Credits earned from LC0200A and/or LC0200B will not be counted towards the "
        "minimum credit units required for graduation.\n"
        "Still Needed:\n"
        "3 credits in LC0200B\n"
        "In-progress\n"
        "Course\tTitle\tGrade\tCredits\tTerm\n"
        "LC0200A\tEnglish for Academic Purposes 1\tIP\t3\tSem A 2026/27\n"
        "Legend\n",
    )
    verified_english = {}
    for code, title in (
        ("LC0200A", "English for Academic Purposes 1"),
        ("LC0200B", "English for Academic Purposes 2"),
    ):
        record = dict(catalogue[code])
        record.update(
            title=title,
            credits=3,
            semesters=["Semester A", "Semester B", "Summer"],
            prerequisites=[],
            exam_percent=0,
            other_percent=100,
            raw_items=["Continuous Assessment: 100%"],
            assessment_unknown=False,
            catalogue_year="2026/27",
            catalogue_detail_available=True,
        )
        verified_english[code] = record
    (english_directory / "courses_db.json").write_text(
        json.dumps(verified_english), encoding="utf-8"
    )
    english_result, _ = run(executable, english_root, "1\nO\nD\n1\n")
    if english_result.returncode != 0:
        raise AssertionError(
            f"LC0200 scheduling flow failed ({english_result.returncode}):\n"
            f"{english_result.stdout}\n{english_result.stderr}"
        )
    english_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", english_result.stdout
    )
    if not english_output:
        raise AssertionError("The LC0200 study plan did not produce a workbook")
    english_workbook = load_workbook(english_output.group(1), data_only=True)
    try:
        schedule = english_workbook["Study Schedule"]
        year_one = year_block(schedule, 1)
        english_a_row = course_row(schedule, "LC0200A")
        english_b_row = course_row(schedule, "LC0200B")
        if not english_a_row or not english_b_row:
            raise AssertionError("An English course was omitted from the schedule")
        semester_a = " ".join(
            str(schedule.cell(english_a_row, column).value or "")
            for column in range(1, 5)
        )
        semester_b = " ".join(
            str(schedule.cell(english_b_row, column).value or "")
            for column in range(5, 9)
        )
        if "LC0200A" not in semester_a:
            raise AssertionError("The in-progress English course was not placed in the current term")
        if "LC0200B" not in semester_b:
            raise AssertionError("LC0200B was not scheduled in a different term from LC0200A")
        if course_remarks(schedule, "LC0200A") not in (None, ""):
            raise AssertionError("The in-progress English course should have blank remarks")
        if "Does not count toward degree credits" not in (
            course_remarks(schedule, "LC0200B") or ""
        ):
            raise AssertionError("Non-degree scheduled English credits were not identified")
        if schedule["B15"].value != 0 or schedule["B16"].value != 6:
            raise AssertionError("Degree credits and total scheduled credits were not distinguished")
        if semester_a.count("LC0200A") != 1 or semester_b.count("LC0200B") != 1:
            raise AssertionError("An English course was repeated or omitted")
    finally:
        english_workbook.close()

    lookup_root = data_root / "lookup-before-planning"
    lookup_directory, _ = user_data_directory(lookup_root)
    prepare_user_data(
        lookup_directory,
        degree_metadata + "Still Needed:\n3 credits in GE2103\n",
    )
    lookup, _ = run(executable, lookup_root, "1\nN\n")
    if lookup.returncode != 0:
        raise AssertionError(
            f"Declining a missing-course lookup should still create a provisional plan:\n"
            f"{lookup.stdout}\n{lookup.stderr}"
        )
    if "Catalogue lookups needed: 1" not in lookup.stdout:
        raise AssertionError(f"Missing course lookup was not estimated:\n{lookup.stdout}")
    if "Type OK to begin these lookups" not in lookup.stdout:
        raise AssertionError(f"Missing course was not offered for online lookup:\n{lookup.stdout}")
    if "Course lookups were cancelled" not in lookup.stdout:
        raise AssertionError("The declined lookup was not identified")
    if "Entry type:" in lookup.stdout or "Topics you do not prefer" in lookup.stdout:
        raise AssertionError("Planning questions appeared before missing-course lookup")
    if "Study plan saved to:" not in lookup.stdout:
        raise AssertionError("Declining a lookup did not produce the workbook")

    full_dynamic_root = data_root / "full-dynamic-cancel"
    full_dynamic_directory, _ = user_data_directory(full_dynamic_root)
    prepare_user_data(
        full_dynamic_directory,
        degree_metadata + "Still Needed:\nAA9001\n",
    )
    full_dynamic, _ = run(executable, full_dynamic_root, "2\nN\n1\nN\n")
    if full_dynamic.returncode != 0:
        raise AssertionError(
            f"Course-update scan cancellation should allow planning to continue:\n"
            f"{full_dynamic.stdout}\n{full_dynamic.stderr}"
        )
    if "Scan for course updates" not in full_dynamic.stdout:
        raise AssertionError("The audit-only course update option was not shown")
    if "Unique Degree Works course codes to check: 1." not in full_dynamic.stdout:
        raise AssertionError("The update scan did not restrict itself to audit course codes")
    if "Course update scan cancelled; no scan requests were started." not in full_dynamic.stdout:
        raise AssertionError("Course update scan cancellation was not reported")
    if "Type 1 to continue to study planning" not in full_dynamic.stdout:
        raise AssertionError("The scan did not return to the in-process continuation prompt")
    if "Course lookups were cancelled" not in full_dynamic.stdout:
        raise AssertionError("The subsequent missing-course lookup was not reported")
    if "Entry type:" in full_dynamic.stdout or "Topics you do not prefer" in full_dynamic.stdout:
        raise AssertionError("Planning questions appeared before the missing-course lookup")
    if "Study plan saved to:" not in full_dynamic.stdout:
        raise AssertionError("Continuing after the scan did not produce the workbook")

    scan_success_root = data_root / f"course-update-scan-success-{time.time_ns()}"
    scan_success_directory, _ = user_data_directory(scan_success_root)
    prepare_user_data(
        scan_success_directory,
        degree_metadata + "Still Needed:\nAA9001\n",
    )
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), CatalogueFixtureHandler
    )
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    CatalogueFixtureHandler.requests = []
    try:
        (scan_success_directory / "overrides.json").write_text(
            json.dumps({
                "course": "AA9001",
                "url": f"http://127.0.0.1:{server.server_port}/AA9001.htm",
            }) + "\n",
            encoding="utf-8",
        )
        scan_success, _ = run(
            executable,
            scan_success_root,
            "2\nOK\n1\n1\n1\n",
        )
    finally:
        server.shutdown()
        server.server_close()
        server_thread.join(timeout=5)
    if scan_success.returncode != 0:
        raise AssertionError(
            "A successful offline-fixture update scan should continue into planning:\n"
            f"{scan_success.stdout}\n{scan_success.stderr}"
        )
    if "Updated course information for AA9001." not in scan_success.stdout:
        raise AssertionError("The audit-only scan did not report the fetched course")
    if CatalogueFixtureHandler.requests != ["/AA9001.htm"]:
        raise AssertionError(
            "The update scan should request the single audit-listed code only"
        )
    updated_catalogue = json.loads(
        (scan_success_directory / "courses_db_scanned.json").read_text(
            encoding="utf-8"
        )
    )
    static_catalogue = json.loads(
        (scan_success_directory / "courses_db.json").read_text(encoding="utf-8")
    )
    if updated_catalogue["AA9001"]["title"] != "Scan Test Course":
        raise AssertionError("The fetched course update was not persisted to the new database")
    if "AA9001" in static_catalogue:
        raise AssertionError("Scanning should not overwrite the old static database")
    if "Study plan saved to:" not in scan_success.stdout:
        raise AssertionError("The user could not continue after a successful scan")
    cache_directory = scan_success_directory / ".cityu_cache"
    cache_directory.mkdir(exist_ok=True)
    (cache_directory / "catalogue-cooldown-until").write_text(
        f"{int(time.time()) + 3600}\n", encoding="utf-8"
    )
    static_first, _ = run(
        executable,
        scan_success_root,
        "1\n1\n1\n",
    )
    if static_first.returncode != 0:
        raise AssertionError(
            "Planning should prefer the scanned database on a later run:\n"
            f"{static_first.stdout}\n{static_first.stderr}"
        )
    if "No online catalogue requests are pending." not in static_first.stdout:
        raise AssertionError("Planning did not treat the scanned record as primary data")
    if "Catalogue cooldown remains" in static_first.stdout:
        raise AssertionError("Planning tried to fetch a course already present in the scanned database")
    static_output = re.search(
        r"Study plan saved to:\s*\n\s*(.+\.xlsx)", static_first.stdout
    )
    if not static_output:
        raise AssertionError("Planning did not create its workbook after scanning")
    static_workbook = load_workbook(static_output.group(1), data_only=True)
    try:
        if "Scan Test Course" not in (
            course_block(static_workbook["Study Schedule"], "AA9001") or ""
        ):
            raise AssertionError("Planning did not use the scanned course details")
    finally:
        static_workbook.close()
    if CatalogueFixtureHandler.requests != ["/AA9001.htm"]:
        raise AssertionError("Planning performed a duplicate request for scanned course data")

    malformed_root = data_root / f"course-update-scan-malformed-{time.time_ns()}"
    malformed_directory, _ = user_data_directory(malformed_root)
    prepare_user_data(
        malformed_directory,
        degree_metadata + "Still Needed:\nAA9002\n",
    )
    malformed_server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), CatalogueFixtureHandler
    )
    malformed_thread = threading.Thread(
        target=malformed_server.serve_forever, daemon=True
    )
    malformed_thread.start()
    (malformed_directory / "overrides.json").write_text(
        json.dumps({
            "course": "AA9002",
            "url": f"http://127.0.0.1:{malformed_server.server_port}/AA9002.htm",
        }) + "\n",
        encoding="utf-8",
    )
    CatalogueFixtureHandler.requests = []
    try:
        malformed_scan, _ = run(
            executable,
            malformed_root,
            "2\nOK\n1\n1\n1\n",
        )
    finally:
        malformed_server.shutdown()
        malformed_server.server_close()
        malformed_thread.join(timeout=5)
    if malformed_scan.returncode != 0:
        raise AssertionError(
            "A malformed course page should be reported and planning should continue:\n"
            f"{malformed_scan.stdout}\n{malformed_scan.stderr}"
        )
    if "did not contain readable credit units" not in malformed_scan.stdout:
        raise AssertionError("A malformed catalogue page was reported as a successful update")
    if "update failures: 1" not in malformed_scan.stdout:
        raise AssertionError("The scan summary omitted its malformed-page failure")
    if CatalogueFixtureHandler.requests != ["/AA9002.htm"]:
        raise AssertionError("A malformed-page failure triggered an unrelated request")
    malformed_catalogue = json.loads(
        (malformed_directory / "courses_db.json").read_text(encoding="utf-8")
    )
    if "AA9002" in malformed_catalogue:
        raise AssertionError("A malformed page should not be saved as a course update")

    blocked_scan_root = data_root / f"course-update-scan-blocked-{time.time_ns()}"
    blocked_scan_directory, _ = user_data_directory(blocked_scan_root)
    prepare_user_data(
        blocked_scan_directory,
        degree_metadata + "Still Needed:\nAA9003\nAA9004\n",
    )
    blocked_scan_server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), CatalogueFixtureHandler
    )
    blocked_scan_thread = threading.Thread(
        target=blocked_scan_server.serve_forever, daemon=True
    )
    blocked_scan_thread.start()
    (blocked_scan_directory / "overrides.json").write_text(
        "\n".join(
            json.dumps({
                "course": code,
                "url": f"http://127.0.0.1:{blocked_scan_server.server_port}/{code}.htm",
            })
            for code in ("AA9003", "AA9004")
        ) + "\n",
        encoding="utf-8",
    )
    CatalogueFixtureHandler.requests = []
    try:
        blocked_scan, _ = run(
            executable,
            blocked_scan_root,
            "2\nOK\n1\n1\n1\n",
        )
    finally:
        blocked_scan_server.shutdown()
        blocked_scan_server.server_close()
        blocked_scan_thread.join(timeout=5)
    if blocked_scan.returncode != 0:
        raise AssertionError(
            "A catalogue block should stop the scan and still permit planning:\n"
            f"{blocked_scan.stdout}\n{blocked_scan.stderr}"
        )
    if "blocked or challenged the scan; remaining requests were stopped" not in blocked_scan.stdout:
        raise AssertionError("The scan did not stop after a block response")
    if "not attempted: 1" not in blocked_scan.stdout:
        raise AssertionError("The scan summary did not report courses skipped after a block")
    if CatalogueFixtureHandler.requests != ["/AA9003.htm"]:
        raise AssertionError("The scan continued making requests after an HTTP 403")

    blocked_root = data_root / "blocked-run"
    blocked_directory, _ = user_data_directory(blocked_root)
    prepare_user_data(
        blocked_directory,
        degree_metadata + "Still Needed:\n3 credits in GE2103\n",
    )
    cache_directory = blocked_directory / ".cityu_cache"
    cache_directory.mkdir(parents=True, exist_ok=True)
    (cache_directory / "catalogue-cooldown-until").write_text(
        f"{int(time.time()) + 3600}\n", encoding="utf-8"
    )
    blocked, _ = run(executable, blocked_root, "2\n1\n")
    if blocked.returncode != 0:
        raise AssertionError(
            f"Incomplete elective data should be retained in the workbook:\n"
            f"{blocked.stdout}\n{blocked.stderr}"
        )
    if "GE2103" not in blocked.stdout:
        raise AssertionError(f"Missing course fields were not identified:\n{blocked.stdout}")
    if "Catalogue cooldown remains" not in blocked.stdout:
        raise AssertionError(f"Cooldown was not connected to the missing data:\n{blocked.stdout}")
    if "no scan requests were started" not in blocked.stdout:
        raise AssertionError("The scan cooldown handling did not prevent network requests")
    if "Entry type:" in blocked.stdout or "Topics you do not prefer" in blocked.stdout:
        raise AssertionError("Preference questions appeared before data feasibility was known")
    if "Study plan saved to:" not in blocked.stdout:
        raise AssertionError("Catalogue cooldown prevented workbook generation")

    delete_root = data_root / "delete-command"
    delete_directory, _ = user_data_directory(delete_root)
    prepare_user_data(delete_directory, "Still Needed:\nCS2310\n")
    (delete_directory / "spark_template.xlsx").write_text("legacy", encoding="utf-8")
    deleted, _ = run(executable, delete_root, "DELETE\ny\n")
    if deleted.returncode != 130:
        raise AssertionError(f"DELETE should confirm and exit:\n{deleted.stdout}\n{deleted.stderr}")
    if any((delete_directory / name).exists() for name in (
        "session_state.json", "page_text.txt", "spark_template.xlsx"
    )):
        raise AssertionError("DELETE did not remove the private files")

    quit_root = data_root / "quit-cancelled"
    quit_directory, _ = user_data_directory(quit_root)
    prepare_user_data(quit_directory, "Still Needed:\nCS2310\n")
    quit_result, _ = run(executable, quit_root, "Q\nN\n1\n1\n1\nO\nD\n")
    if quit_result.returncode != 0:
        raise AssertionError(
            f"Declining Q should preserve data and resume the flow:\n"
            f"{quit_result.stdout}\n{quit_result.stderr}"
        )
    if not (quit_directory / "session_state.json").is_file() or not (
        quit_directory / "page_text.txt"
    ).is_file():
        raise AssertionError("Declining Q deleted private session data")

    privacy_root = data_root / "privacy-cancelled"
    privacy_directory, _ = user_data_directory(privacy_root)
    privacy_directory.mkdir(parents=True, exist_ok=True)
    (privacy_directory / "session_state.json").write_text(
        "preserve", encoding="utf-8"
    )
    privacy_result, _ = run(executable, privacy_root, "1\nC\n")
    if privacy_result.returncode != 130 or "Cancelled." not in privacy_result.stdout:
        raise AssertionError(
            f"C should cancel privacy confirmation:\n"
            f"{privacy_result.stdout}\n{privacy_result.stderr}"
        )
    if (privacy_directory / "session_state.json").read_text(
        encoding="utf-8"
    ) != "preserve":
        raise AssertionError("Cancelling privacy confirmation deleted saved session data")


if __name__ == "__main__":
    main()
