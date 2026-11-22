const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

// Exercise the production QML handler with UI boundary doubles, not a copy of
// its branching logic. Full Qt integration and console tests remain necessary.
const source = fs.readFileSync(path.join(__dirname, '../gui/src/qml/Main.qml'), 'utf8');
const match = source.match(/function onRegistDialogRequested\(host, ps5, duid\) \{[\s\S]*?\r?\n        \}/);
assert.ok(match, 'production registration handler exists');
let checks = 0;

function invoke(host, ps5, duid, accept) {
  const calls = [];
  const context = {
    qsTr: text => text,
    Chiaki: { autoRegister: () => calls.push(['register']) },
    showRegistDialog: (...args) => calls.push(['manual', ...args]),
    root: {
      autoRegister: (...args) => calls.push(['localChoice', ...args]),
      showConfirmDialog: (title, message, yes, no) => {
        calls.push(['confirm', title, message]);
        if (accept) yes();
        else if (no) no();
      }
    }
  };
  vm.createContext(context);
  vm.runInContext(match[0], context);
  context.onRegistDialogRequested(host, ps5, duid);
  return calls;
}

for (const ps5 of [true, false]) {
  const yes = invoke('', ps5, 'account-device-id', true);
  assert.equal(yes[0][1], 'Register via PSN');
  assert.match(yes[0][2], ps5 ? /PS5/ : /main PS4/);
  assert.deepEqual(yes.slice(1), [['register']]);
  checks++;

  const no = invoke('', ps5, 'account-device-id', false);
  assert.equal(no.length, 1, 'declining remote registration must not open a manual dialog');
  checks++;

  assert.deepEqual(invoke('192.0.2.1', ps5, '', true), [['manual', '192.0.2.1', ps5]]);
  checks++;

  for (const accept of [true, false]) {
    const local = invoke('192.0.2.1', ps5, 'account-device-id', accept);
    assert.deepEqual(local.slice(1), [['localChoice', accept, '192.0.2.1', ps5]]);
    checks++;
  }
}
console.log(`${checks} registration UI behavior cases passed.`);
