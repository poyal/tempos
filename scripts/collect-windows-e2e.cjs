// Publish explicit assertions and non-identifying app crops, never user profiles.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const read = file => JSON.parse(fs.readFileSync(path.join(root, file), 'utf8').replace(/^\uFEFF/, ''));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const relative = file => path.relative(root, file).replaceAll('\\', '/');
const source = read('artifacts/windows-real-runs.json');
const builds = read('artifacts/windows-real-builds.json');
const attempts = source.map(run => {
  const result = JSON.parse(fs.readFileSync(path.join(run.folder, 'results.json'), 'utf8').replace(/^\uFEFF/, ''));
  const phase = run.phase || 'before-memory-fix';
  const expected = phase === 'after-memory-fix' ? builds.afterExeSha256 : builds.beforeExeSha256;
  if (result.executableSha256.toLowerCase() !== expected.toLowerCase())
    throw Error('Different production executable: ' + run.mode);
  return { phase, mode: run.mode, source: relative(run.folder), exitCode: run.exitCode,
    started: result.started, completed: result.completed, executableSha256: result.executableSha256,
    passed: result.results.filter(r => r.passed === true).length,
    failed: result.results.filter(r => r.passed === false).length,
    skipped: result.results.filter(r => r.skipped).length, checks: result.results };
});
const finalRuns = [...new Map(attempts.map(run => [run.phase + ':' + run.mode, run])).values()];
if (finalRuns.some(run => run.exitCode !== 0 || run.failed !== 0))
  throw Error('A final run still failed; inspect the original results before publishing');
const windows = [...source].reverse().find(run => run.mode === 'windows-real' && run.phase === 'after-memory-fix');
if (!windows || windows.exitCode !== 0) throw Error('Real Windows suite must finish before publishing captures');
const output = path.join(root, 'docs/validation/windows-2026-10-13');
fs.mkdirSync(output, { recursive: true });
const captures = [];
for (const kind of [0, 1, 2, 3, 4, 6, 8]) {
  const name = 'windows-live-kind-' + kind + '.png';
  const file = path.join(windows.folder, name);
  fs.copyFileSync(file, path.join(output, name));
  captures.push({ file: name, sha256: hash(file), source: relative(windows.folder) });
}
const previous = source.find(run => run.mode === 'windows-real' && !run.phase);
const beforeMemory = path.join(previous.folder, 'windows-live-kind-4.png');
fs.copyFileSync(beforeMemory, path.join(output, 'memory-before-fix.png'));
captures.push({ file: 'memory-before-fix.png', sha256: hash(beforeMemory), source: relative(previous.folder) });
const sum = runs => ({ passed: runs.reduce((n, r) => n + r.passed, 0), failed: runs.reduce((n, r) => n + r.failed, 0),
  skipped: runs.reduce((n, r) => n + r.skipped, 0) });
const after = finalRuns.filter(r => r.phase === 'after-memory-fix');
for (const mode of ['windows-real', 'smoke', 'behavior', 'options']) {
  if (!after.some(run => run.mode === mode)) throw Error('Corrected build is missing ' + mode);
}
const coreLog = fs.readFileSync(path.join(root, 'artifacts/windows-memory-fixed-core.log'), 'utf8');
const corePassed = coreLog.match(/^PASS (\d+) checks\s*$/m);
if (!corePassed) throw Error('Corrected core test result is missing');
const coreBefore = fs.readFileSync(path.join(root, 'artifacts/windows-memory-before.log'), 'utf8').trim();
if (!coreBefore.includes('FAIL after 0: Real memory collector retains consecutive samples for the graph'))
  throw Error('Original memory regression evidence is missing');
const report = {
  date: '2026-10-13', baselineSourceCommit: '79f8387',
  productionSourceNote: 'Real Windows captures exposed missing memory history. The corrected build adds Memory to ProviderService::publish history accumulation. Results are separated by binary hash.',
  correctedSource: ['src/providers.cpp', 'tests/core_tests.cpp', 'tests/e2e/WindowsReal.cs', 'tests/e2e/Regression.cs']
    .map(file => ({ file, sha256: hash(path.join(root, file)) })),
  os: read('artifacts/windows-real-os.json'),
  environment: JSON.parse(fs.readFileSync(path.join(windows.folder, 'windows-environment.json'), 'utf8')),
  packages: { before: {file: builds.beforeZip, sha256: hash(path.join(root, builds.beforeZip)), executableSha256: builds.beforeExeSha256},
    after: {file: builds.afterZip, sha256: hash(path.join(root, builds.afterZip)), executableSha256: builds.afterExeSha256}, freshExtractions: true, fixture: false },
  location: JSON.parse(fs.readFileSync(path.join(windows.folder, 'location-outcome.json'), 'utf8')),
  input: JSON.parse(fs.readFileSync(path.join(windows.folder, 'input-outcome.json'), 'utf8')),
  finalRuns, attempts,
  totals: { baseline: sum(finalRuns.filter(r => r.phase === 'before-memory-fix')), corrected: sum(after) },
  core: { correctedPassed: Number(corePassed[1]), source: 'artifacts/windows-memory-fixed-core.log',
    checks: coreLog.split(/\r?\n/).filter(line => /^PASS \d+ /.test(line) && !/^PASS \d+ checks/.test(line)),
    beforeRegression: { passed: 0, exitCode: 1,
    source: 'artifacts/windows-memory-before.log', failure: 'Real memory collector retains consecutive samples for the graph' } },
  findings: [
    { kind: 'application-defect', issue: 'Memory history was not accumulated by the actual collector. Numeric usage updated but graph and average/maximum had no history.',
      evidence: 'memory-before-fix.png and failing real-collector regression before the fix', fix: 'Include Memory in shared history publication; add consecutive real-sample and average/maximum checks plus an actual graph pixel assertion.' },
    { kind: 'test-defect', issue: 'Options test accepted KiB/s or MiB/s only, missing valid low-traffic B/s rates.',
      fix: 'Require both numeric receive/send rates with the full binary byte-unit range; real rerun observed a B/s rate and passed.' },
    { kind: 'approval-block', issue: 'Automatic approval review rejected real position lookup because coordinate transfer to Open-Meteo lacked explicit consent.',
      handling: 'Ask for consent; default suite never invokes location and records SKIP. Global privacy settings are unchanged.' }
  ],
  captures,
  privacy: 'Only allowlisted widget crops and assertion names are published. No personal profile, PC name, drive labels, network identifiers, coordinates, selected location name, credentials or desktop screenshots are copied.',
  limits: ['OS SendInput, Win+D and position-fix skips are retained as skips, never converted to passes',
    'Real Google OAuth, paid/authenticated weather services, physical monitor disconnection and Windows logon are outside this run',
    'No new performance budget or long-duration leak claim; these are functional Windows E2E runs']
};
fs.writeFileSync(path.join(output, 'results.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({ finalRuns: finalRuns.map(r => ({ phase: r.phase, mode: r.mode, passed: r.passed, failed: r.failed, skipped: r.skipped })), totals: report.totals, captures: captures.length }, null, 2));
