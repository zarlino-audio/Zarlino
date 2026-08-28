-- Migration v14: day-to-day planner tables — calendar, notes, reminders, contacts.
-- The agent's "CEO day" tools: schedule, capture decisions, follow up, track partners.
CREATE TABLE IF NOT EXISTS calendar_events (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  description TEXT,
  start_at TEXT NOT NULL,
  end_at TEXT,
  all_day INTEGER NOT NULL DEFAULT 0,
  location TEXT,
  attendees_json TEXT NOT NULL DEFAULT '[]',
  status TEXT NOT NULL DEFAULT 'SCHEDULED',
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_calendar_start ON calendar_events(start_at);
CREATE INDEX IF NOT EXISTS idx_calendar_status ON calendar_events(status);

CREATE TABLE IF NOT EXISTS notes (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  body TEXT,
  tags_json TEXT NOT NULL DEFAULT '[]',
  pinned INTEGER NOT NULL DEFAULT 0,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_notes_pinned ON notes(pinned);

CREATE TABLE IF NOT EXISTS reminders (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  due_at TEXT NOT NULL,
  done INTEGER NOT NULL DEFAULT 0,
  done_at TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_reminders_due ON reminders(due_at);
CREATE INDEX IF NOT EXISTS idx_reminders_done ON reminders(done);

CREATE TABLE IF NOT EXISTS contacts (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  role TEXT,
  company TEXT,
  email TEXT,
  phone TEXT,
  tags_json TEXT NOT NULL DEFAULT '[]',
  notes TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
