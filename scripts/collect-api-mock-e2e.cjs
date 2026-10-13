const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const relative = file => path.relative(root, file).replaceAll('\\', '/');
const manifest = read(path.join(root, 'artifacts/api-mock-runs.json'));
const attempts = manifest.map(run => {
  const result = read(path.join(run.folder, 'results.json'));
  const audit = path.join(run.folder, 'api-mock-requests.jsonl');
  const requests = fs.existsSync(audit) ? fs.readFileSync(audit, 'utf8').trim().split(/\r?\n/).filter(Boolean).map(JSON.parse) : [];
  if (result.apiMode !== 'mock' || result.testClass !== 'e2e') throw Error('Unexpected API boundary');
  if (!requests.every(call => call.matched && call.transport === 'mock')) throw Error('Unexpected API request');
  return { source: relative(run.folder), phase: run.phase || 'initial', exitCode: run.exitCode, ...result,
    requests, passed: result.results.filter(check => check.passed === true).length,
    failed: result.results.filter(check => check.passed === false).length,
    skipped: result.results.filter(check => check.skipped).length };
});
const final = [...new Map(attempts.filter(run => run.phase === 'final-policy').map(run => [run.mode, run])).values()];
for (const mode of ['--api-mock', '--weather', '--windows-real'])
  if (!final.some(run => run.mode === mode && run.exitCode === 0 && run.failed === 0)) throw Error('Final suite incomplete: ' + mode);
const exeHash = hash(path.join(root, 'out/review/Tempos.TestHost.exe'));
if (final.some(run => run.executableSha256.toLowerCase() !== exeHash)) throw Error('Final executable hash changed');
const coreLog = fs.readFileSync(path.join(root, 'artifacts/api-mock-core.log'), 'utf8');
const corePassed = coreLog.match(/^PASS (\d+) checks\s*$/m);
if (!corePassed) throw Error('Core test result missing');
const guard = read(path.join(root, 'artifacts/api-mock-guard.json'));
const rejected = read(path.join(guard.folder, 'results.json'));
if (guard.exitCode !== 1 || rejected.results.length !== 1 || !rejected.results[0].name.includes('E2E requires Tempos.TestHost.exe'))
  throw Error('Production launch guard was not verified');
const report = {
  date: '2026-10-13', policy: 'Real Windows E2E with mocked external APIs and location; live API integration is separate.',
  binarySha256: exeHash, runnerSha256: hash(path.join(root, 'artifacts/api-mock-runner/Tempos.E2E.dll')),
  core: { passed: Number(corePassed[1]), executableSha256: hash(path.join(root, 'out/review/tempos_tests.exe')),
    checks: coreLog.split(/\r?\n/).filter(line => /^PASS \d+ /.test(line) && !/^PASS \d+ checks/.test(line)) },
  finalModes: final.map(run => ({mode: run.mode, source: run.source, passed: run.passed, failed: run.failed, skipped: run.skipped})),
  totals: {passed: final.reduce((n, run) => n + run.passed, 0), failed: final.reduce((n, run) => n + run.failed, 0), skipped: final.reduce((n, run) => n + run.skipped, 0)},
  productionLaunchGuard: { passed: true, source: relative(guard.folder), expectedExitCode: 1, appLaunched: false, result: rejected },
  attempts,
  location: read(path.join(root, final.find(run => run.mode === '--windows-real').source, 'location-outcome.json')),
  initialFailure: 'The first API suite inspected accessibility before calendar presentation refreshed. Apply the selected widget after the provider result arrives; retain the refresh interval and verify its actual accessible event text.',
  limits: ['Mock OAuth validates the app loopback/token/event pipeline, not Google login or remote consent screens.',
    'Real API availability and actual geolocation are not E2E completion requirements.',
    'OS input skips remain explicit; no new package or live API probe was published.'],
  privacy: 'Request records contain endpoint routes and mock scenario names only; no query strings, tokens, real accounts, actual coordinates or desktop images.'
};
fs.writeFileSync(path.join(root, 'docs/validation/2026-10-13-api-mock.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({core: report.core.passed, finalModes: report.finalModes, totals: report.totals, location: report.location}, null, 2));
