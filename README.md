# CoursePilot – Qt Desktop Organizer

CoursePilot is a cross-platform desktop app for students to manage semesters, courses, and assignments in one place. It features login/password authentication, persistent storage via SQLite, and a dashboard view with courses, assignments, and an “Upcoming Deadlines” panel powered by a priority queue.

---

## Features
- **User Authentication:** Register/login with credentials securely hashed and stored locally.
- **Semester & Course Management:** Add semesters (term/year), courses (code, name, color), and assignments (type, title, due date, topics, notes).
- **Dashboard:** View all courses, assignments, and a prioritized list of upcoming deadlines.
- **Persistent Storage:** All data is stored in a local SQLite database (no setup required).
- **Cross-Platform:** Built with Qt 6 (Widgets + Sql), tested on macOS, and should work on Linux/Windows with minor adjustments.

---

## Installation

### Prerequisites
- **Qt 6.5+** (Widgets and Sql modules)
- **CMake** (version 3.22+)
- **Ninja** (recommended for fast builds)
- **Homebrew** (for macOS, to install Qt)

### Build Instructions (macOS example)
```sh
brew install qt@6 ninja cmake
mkdir -p build && cd build
cmake -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)" -DCMAKE_BUILD_TYPE=Debug ..
ninja
open ./coursepilot_single.app
```
For Linux/Windows, adjust the `CMAKE_PREFIX_PATH` to your Qt installation location.

---

## Usage
- On first launch, register a new account.
- Add semesters, then courses, then assignments.
- The dashboard displays all items and upcoming deadlines (sorted by due date).
- All data is stored locally; no sample database is provided.

---

## Limitations & Roadmap
- Only due dates are tracked (no durations/conflict detection yet).
- No cloud sync; export/import is limited to the local SQLite database.
- UI is minimalistic (basic Qt Widgets).
- Future improvements: screenshots, demo video, cloud sync, richer UI, schedule conflict detection.

---

## License
MIT License. See [LICENSE](LICENSE) for details.
