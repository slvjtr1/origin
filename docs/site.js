/* Origin site: the language (English or Arabic), copy buttons, a toast */
(function () {
  var KEY = 'origin.lang';
  function pick() {
    var q = /[?&]lang=(en|ar)/.exec(location.search); if (q) return q[1];
    try { var s = localStorage.getItem(KEY); if (s === 'en' || s === 'ar') return s; } catch (e) {}
    return /^ar\b/i.test(navigator.language || '') ? 'ar' : 'en';
  }
  function set(l) { document.documentElement.lang = l; document.documentElement.dir = l === 'ar' ? 'rtl' : 'ltr'; try { localStorage.setItem(KEY, l); } catch (e) {} }
  set(pick());
  window.originLang = function () { return document.documentElement.lang; };
  function toast(msg) {
    var t = document.querySelector('.toast'); if (!t) { t = document.createElement('div'); t.className = 'toast'; document.body.appendChild(t); }
    t.textContent = msg; t.classList.add('on'); clearTimeout(t._h); t._h = setTimeout(function () { t.classList.remove('on'); }, 1800);
  }
  window.originToast = toast;
  window.originCopy = function (text) {
    var done = function () { toast(originLang() === 'ar' ? 'تم النسخ' : 'Copied'); };
    if (navigator.clipboard && window.isSecureContext) navigator.clipboard.writeText(text).then(done, function () { fallback(); });
    else fallback();
    function fallback() { var a = document.createElement('textarea'); a.value = text; a.style.position = 'fixed'; a.style.opacity = '0'; document.body.appendChild(a); a.select(); try { document.execCommand('copy'); done(); } catch (e) {} document.body.removeChild(a); }
  };
  document.addEventListener('click', function (e) {
    var b = e.target.closest('[data-lang-toggle]'); if (b) { set(originLang() === 'ar' ? 'en' : 'ar'); document.dispatchEvent(new Event('originlang')); return; }
    var c = e.target.closest('[data-copy]'); if (c) { var src = document.getElementById(c.getAttribute('data-copy')); originCopy(src ? (src.value || src.textContent) : c.getAttribute('data-copy')); }
  });
})();
