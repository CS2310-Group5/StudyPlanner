import argparse
import json
import sys
from collections import defaultdict

try:
    from openpyxl import Workbook
    from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
    from openpyxl.utils import get_column_letter
    from openpyxl.worksheet.page import PageMargins
except ImportError as exc:
    print("Excel export requires openpyxl. Install it with your project Python:", exc, file=sys.stderr)
    raise SystemExit(3)


parser = argparse.ArgumentParser()
parser.add_argument("--plan", required=True)
parser.add_argument("--output", required=True)
args = parser.parse_args()

with open(args.plan, "r", encoding="utf-8") as plan_file:
    plan = json.load(plan_file)

plan_years = max(1, min(4, int(plan.get("plan_years", 4))))
terms = []
for year in range(1, plan_years + 1):
    terms.extend(
        [
            (f"Year {year} Semester A", "Sem A"),
            (f"Year {year} Semester B", "Sem B"),
            (f"Year {year} Summer", "Summer"),
        ]
    )

courses_by_term = defaultdict(list)
credits_by_term = defaultdict(int)
manual_courses = []
valid_terms = {term for term, _ in terms}
for course in plan["courses"]:
    if course.get("manual_placement", False):
        manual_courses.append(course)
        continue
    place = course["place"].split(" (", 1)[0]
    if place not in valid_terms:
        raise ValueError(f"Course {course['code']} has an invalid semester: {place}")
    courses_by_term[place].append(course)
    credits_by_term[place] += int(course.get("term_credits", 0))

planned_course_credits = sum(
    int(course.get("term_credits", 0))
    for course in plan["courses"]
    if not course.get("manual_placement", False)
)
degree_course_credits = sum(
    int(course.get("term_credits", 0))
    for course in plan["courses"]
    if not course.get("manual_placement", False)
    and course.get("counts_toward_degree", True)
)

workbook = Workbook()
sheet = workbook.active
sheet.title = "Study Schedule"
sheet.sheet_view.showGridLines = False

navy = "17365D"
blue = "D9EAF7"
light_blue = "EEF5FB"
warning_fill = "FFF2CC"
white = "FFFFFF"
border_side = Side(style="medium", color="7F8FA6")
soft_border_side = Side(style="thin", color="B4C7E7")
last_column = 12
last_letter = get_column_letter(last_column)
sheet.merge_cells(f"A1:{last_letter}1")
title = sheet["A1"]
title.value = (
    "Catalogue Demo — Not a Degree Plan"
    if plan.get("illustrative")
    else "Study Schedule"
)
title.font = Font(name="Aptos Display", size=18, bold=True, color=white)
title.fill = PatternFill("solid", fgColor=navy)
title.alignment = Alignment(vertical="center")
sheet.row_dimensions[1].height = 32


def display(value):
    return value if value not in (None, "") else "Not provided in Degree Works export"


metadata = [
    ("Student name", display(plan.get("student_name"))),
    ("Student ID", display(plan.get("student_id"))),
    ("Programme", display(plan.get("programme"))),
    ("Major", display(plan.get("major"))),
    ("Degree", display(plan.get("degree"))),
    ("College", display(plan.get("college"))),
    ("Department", display(plan.get("department"))),
    ("Cohort", display(plan.get("cohort"))),
    ("Audit date", display(plan.get("audit_date"))),
    ("Expected graduation", display(plan.get("expected_graduation_date"))),
    ("Degree Works current semester", display(plan.get("current_semester"))),
    ("Plan duration", f"{plan_years} years"),
    ("Degree credits planned", degree_course_credits),
    ("Total scheduled credits", planned_course_credits),
    ("Degree Works maximum credits", plan.get("max_credits_permitted") or "Not provided in Degree Works export"),
    ("Workbook status", "Planning aid"),
]

for row_number, (label, value) in enumerate(metadata, start=3):
    label_cell = sheet.cell(row_number, 1, label)
    label_cell.font = Font(bold=True, color=navy)
    value_cell = sheet.cell(row_number, 2, value)
    value_cell.alignment = Alignment(wrap_text=True, vertical="top")
    for column in range(1, last_column + 1):
        sheet.cell(row_number, column).fill = PatternFill(
            "solid", fgColor=light_blue if row_number % 2 else white
        )
        sheet.cell(row_number, column).border = Border(bottom=border_side)
    sheet.merge_cells(start_row=row_number, start_column=2,
                      end_row=row_number, end_column=last_column)

plan_warnings = [str(value) for value in plan.get("warnings", []) if value]
year_start_row = len(metadata) + 4
if plan_warnings:
    sheet.merge_cells(
        start_row=year_start_row, start_column=1,
        end_row=year_start_row, end_column=last_column
    )
    warning_cell = sheet.cell(year_start_row, 1)
    warning_cell.value = "PLANNING WARNINGS\n" + "\n".join(
        f"• {warning}" for warning in plan_warnings
    )
    warning_cell.font = Font(bold=True, color="7F6000")
    warning_cell.fill = PatternFill("solid", fgColor=warning_fill)
    warning_cell.alignment = Alignment(wrap_text=True, vertical="top")
    warning_cell.border = Border(bottom=border_side)
    sheet.row_dimensions[year_start_row].height = min(180, 30 + 18 * len(plan_warnings))
    year_start_row += 2

year_rows = {}
cursor = year_start_row
term_definitions = (
    ("Sem A", "Semester A"),
    ("Sem B", "Semester B"),
    ("Summer", "Summer"),
)


def short_remarks(course):
    if course.get("requirement") == "In progress":
        return ""
    requirement = course.get("requirement", "")
    if "Elective" in requirement:
        remarks = ["Elective"]
    elif "major" in requirement.lower():
        remarks = ["Major requirement"]
    elif requirement == "Degree Works required":
        remarks = ["Required"]
    else:
        remarks = []
    if not course.get("credits_known", True):
        remarks.append("⚠️ 3 credits assumed")
    missing_prerequisites = [
        code
        for code, status in course.get("prerequisite_statuses", {}).items()
        if status == "not listed" and code != "UNKNOWN"
    ]
    if missing_prerequisites:
        remarks.append("⚠️ Missing " + ", ".join(missing_prerequisites))
    for warning in course.get("warnings", []):
        text = str(warning).lower()
        if "prerequisite" in text:
            continue
        if "offering" in text or "summer" in text:
            remarks.append("⚠️ Confirm offering")
        elif "credit limit" in text:
            remarks.append("⚠️ Credit limit")
        elif "english" in text:
            remarks.append("⚠️ English-course timing")
    if not course.get("counts_toward_degree", True):
        remarks.append("Does not count toward degree credits")
    return "; ".join(dict.fromkeys(remarks))


def outline(row_start, row_end, column_start, column_end):
    for row in range(row_start, row_end + 1):
        for column in range(column_start, column_end + 1):
            cell = sheet.cell(row, column)
            cell.border = Border(
                left=border_side if column == column_start else cell.border.left,
                right=border_side if column == column_end else cell.border.right,
                top=border_side if row == row_start else cell.border.top,
                bottom=border_side if row == row_end else cell.border.bottom,
            )


for year in range(1, plan_years + 1):
    year_top = cursor
    year_rows[year] = cursor
    year_cell = sheet.cell(cursor, 1, f"Year {year}")
    sheet.merge_cells(start_row=cursor, start_column=1,
                      end_row=cursor, end_column=last_column)
    year_cell.font = Font(bold=True, color=white, size=13)
    year_cell.fill = PatternFill("solid", fgColor=navy)
    cursor += 1

    heading_row = cursor
    for term_index, (heading, term_name) in enumerate(term_definitions):
        first_column = term_index * 4 + 1
        last_term_column = first_column + 3
        sheet.merge_cells(start_row=heading_row, start_column=first_column,
                          end_row=heading_row, end_column=last_term_column)
        cell = sheet.cell(heading_row, first_column, heading)
        cell.font = Font(bold=True, color=white)
        cell.fill = PatternFill("solid", fgColor=navy)
        cell.alignment = Alignment(horizontal="center")
        for column, label in enumerate(("Code", "Course", "Credits", "Remarks"),
                                       start=first_column):
            subheader = sheet.cell(heading_row + 1, column, label)
            subheader.font = Font(bold=True, color=navy)
            subheader.fill = PatternFill("solid", fgColor=blue)
            subheader.alignment = Alignment(wrap_text=True, horizontal="center")
            subheader.border = Border(bottom=border_side)
    cursor += 2
    course_start = cursor
    semester_courses = [
        courses_by_term[f"Year {year} {term_name}"]
        for _, term_name in term_definitions
    ]
    visible_rows = max(1, len(semester_courses[0]), len(semester_courses[1]))
    for item_index in range(visible_rows):
        for term_index, (_, term_name) in enumerate(term_definitions):
            base = term_index * 4 + 1
            entries = semester_courses[term_index]
            if term_index == 2:
                values = ["", "", "", ""]
            elif item_index < len(entries):
                course = entries[item_index]
                values = [
                    course.get("code", ""),
                    course.get("title", ""),
                    int(course.get("term_credits", 0)),
                    short_remarks(course),
                ]
            elif item_index == 0:
                values = ["", "No courses planned", "", ""]
            else:
                values = ["", "", "", ""]
            for offset, value in enumerate(values):
                cell = sheet.cell(cursor, base + offset, value)
                cell.alignment = Alignment(vertical="top", wrap_text=True)
                if offset == 2:
                    cell.alignment = Alignment(horizontal="center", vertical="top")
                cell.fill = PatternFill(
                    "solid", fgColor=light_blue if term_index % 2 == 0 else white
                )
                cell.border = Border(
                    left=border_side if offset == 0 else soft_border_side,
                    right=border_side if offset == 3 else soft_border_side,
                    bottom=border_side,
                )
                if "Prerequisite " in str(value):
                    cell.font = Font(color="808080")
        sheet.row_dimensions[cursor].height = 28
        cursor += 1

    total_row = cursor
    year_total = 0
    for term_index, (_, term_name) in enumerate(term_definitions):
        base = term_index * 4 + 1
        term = f"Year {year} {term_name}"
        credits = credits_by_term[term]
        if term_index < 2:
            year_total += credits
        sheet.cell(total_row, base, "Semester total")
        sheet.cell(total_row, base + 2, credits)
        sheet.cell(total_row, base + 3, "credits")
        for column in range(base, base + 4):
            cell = sheet.cell(total_row, column)
            cell.font = Font(bold=True, color=navy)
            cell.fill = PatternFill("solid", fgColor=blue)
            cell.border = Border(
                left=border_side if column == base else soft_border_side,
                right=border_side if column == base + 3 else soft_border_side,
                top=border_side,
            )
    cursor += 1
    sheet.merge_cells(start_row=cursor, start_column=1,
                      end_row=cursor, end_column=last_column)
    summary = sheet.cell(cursor, 1, f"Year {year} total: {year_total} credits")
    summary.font = Font(bold=True, color=navy)
    summary.fill = PatternFill("solid", fgColor=light_blue)
    year_bottom = cursor
    for term_index in range(3):
        first_column = term_index * 4 + 1
        outline(heading_row, total_row, first_column, first_column + 3)
    outline(year_top, year_bottom, 1, last_column)
    cursor += 2

sheet.freeze_panes = f"A{year_start_row + 3}"
sheet.sheet_properties.pageSetUpPr.fitToPage = True
sheet.page_setup.orientation = "landscape"
sheet.page_setup.fitToWidth = 0
sheet.page_setup.fitToHeight = 0
sheet.page_margins = PageMargins(left=0.25, right=0.25, top=0.5, bottom=0.5)
sheet.print_title_rows = "1:1"
for base in (1, 5, 9):
    sheet.column_dimensions[get_column_letter(base)].width = 13
    sheet.column_dimensions[get_column_letter(base + 1)].width = 27
    sheet.column_dimensions[get_column_letter(base + 2)].width = 10
    sheet.column_dimensions[get_column_letter(base + 3)].width = 24
sheet.sheet_format.defaultRowHeight = 20
sheet.print_area = f"A1:{last_letter}{cursor - 1}"

manual = workbook.create_sheet("Manual Placement")
manual.sheet_view.showGridLines = False
manual.append(["Course Code", "Course Name", "Credits", "Remarks"])
for cell in manual[1]:
    cell.font = Font(bold=True, color=white)
    cell.fill = PatternFill("solid", fgColor=navy)
for course in manual_courses:
    remarks = short_remarks(course)
    extra = "; ".join(str(item) for item in course.get("warnings", []))
    if extra:
        remarks = "; ".join(value for value in (remarks, extra) if value)
    credits = int(course.get("credits", 3))
    if not course.get("credits_known", True):
        credits = f"{credits}*"
    manual.append([
        course.get("code", ""),
        course.get("title", ""),
        credits,
        remarks,
    ])
if not manual_courses:
    manual.append(["", "No courses require manual placement", "", ""])
for column, width in enumerate((16, 38, 12, 72), start=1):
    manual.column_dimensions[get_column_letter(column)].width = width
for row in manual.iter_rows(min_row=2):
    for cell in row:
        cell.alignment = Alignment(vertical="top", wrap_text=True)
        cell.border = Border(bottom=border_side)

electives = workbook.create_sheet("Elective Selections")
electives.sheet_view.showGridLines = False
electives.merge_cells("A1:G1")
electives["A1"] = "Elective Selections"
electives["A1"].font = Font(name="Aptos Display", size=16, bold=True, color=white)
electives["A1"].fill = PatternFill("solid", fgColor=navy)
electives.append(
    ["Choice Group", "Requirement", "Selected Course", "Course Title", "Credit Units", "Course Information", "Prerequisites"]
)
for cell in electives[2]:
    cell.font = Font(bold=True, color=navy)
    cell.fill = PatternFill("solid", fgColor=blue)
    cell.alignment = Alignment(wrap_text=True)
course_records = {course["code"]: course for course in plan["courses"]}
selection_row = 3
for index, selection in enumerate(plan.get("elective_selections", []), start=1):
    codes = selection.get("courses", [])
    if not codes:
        electives.append(
            [
                f"Choice group {index}",
                selection.get("requirement", "Degree Works choice"),
                "Not selected",
                selection.get("note", "No selection was recorded."),
                "",
                "",
                "",
            ]
        )
        selection_row += 1
        continue
    for code in codes:
        course = course_records.get(code)
        if not course:
            electives.append(
                [f"Choice group {index}", selection["requirement"], code,
                 "Course details not available", "Unverified", "", ""]
            )
        else:
            url = course.get("url") or (
                f"https://www.cityu.edu.hk/catalogue/ug/current/course/{code}.htm"
            )
            prerequisite_statuses = course.get("prerequisite_statuses", {})
            prerequisite_text = "; ".join(
                f"{prerequisite}: {status}"
                for prerequisite, status in prerequisite_statuses.items()
            )
            electives.append(
                [
                    f"Choice group {index}",
                    selection["requirement"],
                    code,
                    course.get("title") or "Title not available",
                    course["credits"] if course.get("credits_known", True)
                    else f"{course.get('credits', 3)}*",
                    url,
                    prerequisite_text,
                ]
            )
        selection_row += 1
if selection_row == 3:
    electives.cell(3, 1, "No elective choices were selected for this plan.")
for column, width in enumerate([20, 32, 20, 48, 18, 72, 48], start=1):
    electives.column_dimensions[get_column_letter(column)].width = width
electives.freeze_panes = "A3"
electives.auto_filter.ref = f"A2:G{max(2, selection_row - 1)}"
for row_cells in electives.iter_rows(min_row=3):
    for cell in row_cells:
        cell.alignment = Alignment(vertical="top", wrap_text=True)
        cell.border = Border(bottom=border_side)
        if cell.column == 7 and cell.value:
            cell.font = Font(color="808080")

notes = workbook.create_sheet("Notes")
notes.sheet_view.showGridLines = False
notes.append(["Planning notes"])
notes["A1"].font = Font(size=16, bold=True, color=white)
notes["A1"].fill = PatternFill("solid", fgColor=navy)
notes.append(["This workbook is a planning aid, not an official degree audit."])
notes.append(["Confirm degree requirements, course availability, and prerequisites with your own Degree Works record and official university sources."])
notes.append(["Year 1 Semester A contains the Degree Works In-progress courses; other courses start in a later semester."])
notes.append(["Summer boxes are blank. Internship and Summer-only courses are listed on Manual Placement."])
notes.append(["Project courses are placed in the final year."])
notes.append(["Courses with missing offering terms are provisionally placed in Semester A or Semester B; confirm availability before registration."])
notes.append(["An asterisk (*) means missing credit units defaulted to 3 credits."])
notes.append(["Personal details are read from the local Degree Works export and remain in this workbook on your computer."])
notes.append(["No course dropdowns or programme-specific graduation rules are built into this workbook."])
for warning in plan_warnings:
    notes.append([f"Warning: {warning}"])
if plan.get("illustrative"):
    notes.append(["Offline demo: catalogue records are real; the selected course is only an example and is not based on a student's Degree Works audit."])
notes.column_dimensions["A"].width = 110
for cell in notes["A"]:
    cell.alignment = Alignment(wrap_text=True, vertical="top")
notes.row_dimensions[3].height = 36

workbook.save(args.output)
