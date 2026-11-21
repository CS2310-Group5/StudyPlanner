import json
import os
import re
import subprocess
import sys
from pathlib import Path

from openpyxl import load_workbook


def main():
    executable = Path(sys.argv[1]).resolve()
    data_root = Path(sys.argv[2]).resolve()
    catalogue = json.loads(Path(sys.argv[3]).read_text(encoding="utf-8"))
    environment = os.environ.copy()

    if sys.platform == "win32":
        environment["LOCALAPPDATA"] = str(data_root)
        user_data = data_root / "CityUStudyPlan"
    elif sys.platform == "darwin":
        environment["HOME"] = str(data_root)
        user_data = data_root / "Library" / "Application Support" / "CityUStudyPlan"
    else:
        environment["XDG_DATA_HOME"] = str(data_root)
        user_data = data_root / "cityu-study-plan"

    result = subprocess.run(
        [str(executable), "--demo"],
        capture_output=True,
        text=True,
        env=environment,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(f"Demo failed:\n{result.stdout}\n{result.stderr}")
    if "Elective selected:" not in result.stdout:
        raise AssertionError("Demo should identify the selected catalogue course")
    if "not a degree requirement" not in result.stdout:
        raise AssertionError("Demo should distinguish catalogue samples from requirements")

    match = re.search(r"Study plan saved to:\s*\n\s*(.+\.xlsx)", result.stdout)
    if not match:
        raise AssertionError(f"Demo did not report an Excel workbook:\n{result.stdout}")
    if "Wrote " in result.stdout:
        raise AssertionError("Demo printed the workbook path more than once")

    workbook_path = Path(match.group(1))
    if not workbook_path.is_file():
        raise AssertionError(f"Demo workbook was not created: {workbook_path}")
    expected_directory = user_data / "Study Plans"
    if workbook_path.parent != expected_directory:
        raise AssertionError(f"Workbook was saved outside the Study Plans folder: {workbook_path}")

    workbook = load_workbook(workbook_path, data_only=True)
    try:
        if workbook.sheetnames != [
            "Study Schedule", "Manual Placement", "Elective Selections", "Notes"
        ]:
            raise AssertionError(f"Unexpected workbook sheets: {workbook.sheetnames}")
        sheet = workbook["Study Schedule"]
        if sheet["A1"].value != "Catalogue Demo — Not a Degree Plan":
            raise AssertionError("Demo workbook should not be presented as a personalized degree plan")
        year_rows = {}
        for row in range(1, sheet.max_row + 1):
            value = sheet.cell(row, 1).value
            if isinstance(value, str) and re.fullmatch(r"Year [1-4]", value):
                year_rows[int(value[-1])] = row
        if sorted(year_rows) != [1, 2, 3, 4]:
            raise AssertionError(f"Academic years were not separated vertically: {year_rows}")
        expected_headings = ["Sem A", "Sem B", "Summer"]
        for year, year_row in year_rows.items():
            headings = [
                sheet.cell(year_row + 1, column).value for column in (1, 5, 9)
            ]
            if headings != expected_headings:
                raise AssertionError(f"Unexpected semester columns for Year {year}: {headings}")
        if sheet["A3"].value != "Student name" or "Not provided" not in sheet["B3"].value:
            raise AssertionError("Missing or blank Degree Works metadata fields")
        if sheet.data_validations.dataValidation:
            raise AssertionError("Study schedule must not use course dropdown lists")
        for year_row in year_rows.values():
            if not any(
                sheet.cell(year_row + 3, column).value
                for column in (1, 2, 5, 6)
            ):
                raise AssertionError("Semester boxes should show a course or an explicit status")
            if sheet.cell(year_row + 3, 12).value not in (None, ""):
                raise AssertionError("Summer boxes should remain blank")
        if not any(
            sheet.cell(year_row + 3, column).value == "No courses planned"
            for year_row in year_rows.values()
            for column in (2, 6)
        ):
            raise AssertionError("Unused semesters should be labelled")

        elective_rows = list(
            workbook["Elective Selections"].iter_rows(min_row=3, values_only=True)
        )
        selected_codes = {
            row[2] for row in elective_rows if row and row[2] in catalogue
        }
        if not selected_codes:
            raise AssertionError("Elective Selections sheet does not list a selected course")
        notes = [cell.value for row in workbook["Notes"].iter_rows() for cell in row]
        if not any(
            isinstance(value, str) and "not based on a student's Degree Works audit" in value
            for value in notes
        ):
            raise AssertionError("Workbook notes should explain that the demo is illustrative")
    finally:
        workbook.close()

    course_rows = [
        row for sheet_name in ("Study Schedule", "Manual Placement")
        for row in workbook[sheet_name].iter_rows(values_only=True)
        if any(value in catalogue for value in row)
    ]
    if len(course_rows) != 1:
        raise AssertionError("Demo should show one genuine bundled catalogue course")
    code = next(value for value in course_rows[0] if value in catalogue)
    if code not in catalogue:
        raise AssertionError(f"Demo course is not from the bundled database: {code}")
    if catalogue[code].get("title") not in course_rows[0]:
        raise AssertionError("Demo workbook title does not match the bundled catalogue")
    if catalogue[code].get("credits") not in course_rows[0]:
        raise AssertionError("Demo workbook credits do not match the bundled catalogue")


if __name__ == "__main__":
    main()
