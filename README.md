# StudyPlanner

StudyPlanner is a C++ command-line project for planning courses at CityUHK.
Students need to line up course offerings, precursor requirements, and credit
limits across semesters. The planned scheduler will use those constraints to
suggest a semester-by-semester plan and explain why a course could not fit.

## What runs today

The current build reads [the sample course catalogue](data/courses.csv) and
prints the course code, credits, academic year, listed offering semesters, and
precursor options for each course. The automatic planner and Excel export are
planned features.

## Run the catalogue preview

You need a C++17 compiler. In VS Code, open a PowerShell terminal in the
StudyPlanner repository root, where `main.cpp` and `data/` are located. Run:

```powershell
g++ -std=c++17 -Wall -Wextra -pedantic main.cpp -o "$env:TEMP\study-planner.exe"
& "$env:TEMP\study-planner.exe"
```

The sample produces:

```text
Loaded 2 course(s).
CS2310 | 3 credits | 2026/27 | offered: A|B | precursor options: CS1102|CS1302|CS1302A
CS2312 | 3 credits | 2026/27 | offered: A|B | precursor options: CS2310|CS2311
```

`A|B` means the catalogue lists both semesters for that academic year.
`CS2310|CS2311` means either course is listed as a precursor option for CS2312.
To preview another CSV with the same five columns, pass its path as the first
argument to the executable.

The sample comes from the CityUHK catalogue entries for
[CS2310](https://www.cityu.edu.hk/catalogue/ug/current/course/CS2310.htm) and
[CS2312](https://www.cityu.edu.hk/catalogue/ug/current/course/CS2312.htm).
Listed offering semesters are subject to change.

## Planned study plan example

A student who has completed CS1302 wants to take CS2310 and CS2312. With enough
room in the credit limit, the planner could suggest CS2310 in Semester A and
CS2312 in Semester B because CS2310 satisfies one of CS2312's precursor options.
If the student has already completed CS2311, the planner could suggest CS2312
as early as Semester A and explain the change.
