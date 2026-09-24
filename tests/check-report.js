// Shared by the test pages: shows a program's output, and reports its outcome the
// same way everywhere — to a person as a PASS/FAIL line and in the page title, and
// to tools/run-checks.mjs as window.checkResult = {status: 'pass'|'fail', detail}.
//
// A page that runs an Emscripten test program uses checkModule() for its Module:
//   <script src="check-report.js"></script>
//   <script>var Module = checkModule({canvas: document.getElementById('canvas')});</script>
//   <script src="some_contract.js"></script>
// The program passes by exiting with status 0; a non-zero exit, an abort or an
// uncaught error fails it. Pages with their own logic call reportCheck() instead.
(function () {
  function output() {
    return document.getElementById('output');
  }

  // Also to the console, where tools/run-checks.mjs reads it.
  function log(line) {
    const element = output();
    if (element) element.textContent += line + '\n';
    console.log(line);
  }

  function reportCheck(status, detail) {
    if (window.checkResult) return;
    window.checkResult = { status, detail: detail || '' };
    document.title = (status === 'pass' ? 'PASS' : 'FAIL') + ' · ' + document.title;
    log((status === 'pass' ? 'PASS' : 'FAIL') + (detail ? ': ' + detail : ''));
  }

  function checkModule(extra) {
    return Object.assign(
      {
        print: log,
        printErr: log,
        onAbort: (what) => {
          log('ABORT ' + what);
          reportCheck('fail', 'aborted: ' + what);
        },
        onExit: (code) => {
          log('Exit ' + code);
          reportCheck(code === 0 ? 'pass' : 'fail', 'exit code ' + code);
        },
      },
      extra || {},
    );
  }

  window.addEventListener('error', (event) => reportCheck('fail', 'uncaught error: ' + event.message));
  window.addEventListener('unhandledrejection', (event) =>
    reportCheck('fail', 'unhandled rejection: ' + ((event.reason && event.reason.stack) || event.reason)),
  );
  window.log = log;
  window.reportCheck = reportCheck;
  window.checkModule = checkModule;
})();
