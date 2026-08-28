import { createMemorySql } from '../src/db/sql';
import { migrate } from '../src/db/migrations';

const sql = createMemorySql();
try {
  const applied = migrate(sql);
  console.log('migrated:', JSON.stringify(applied));
  // sanity: does agent_runs have a status column?
  const cols = sql.all('PRAGMA table_info(agent_runs)') as { name: string }[];
  console.log('agent_runs cols:', cols.map((c) => c.name).join(','));
} catch (e) {
  console.log('MIGRATE ERROR:', (e as Error).message);
}
