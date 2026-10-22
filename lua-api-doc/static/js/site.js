(function () {
  var toggle = document.getElementById("sidebar-toggle");
  var sidebar = document.getElementById("sidebar");
  if (toggle && sidebar) {
    toggle.addEventListener("click", function () {
      sidebar.classList.toggle("open");
    });
    sidebar.querySelectorAll("a").forEach(function (a) {
      a.addEventListener("click", function () { sidebar.classList.remove("open"); });
    });
  }

  var filter = document.getElementById("nav-filter");
  if (filter) {
    filter.addEventListener("input", function () {
      var q = filter.value.trim().toLowerCase();
      document.querySelectorAll(".nav-tree .nav-page").forEach(function (li) {
        var text = li.textContent.toLowerCase();
        li.classList.toggle("hidden", q.length > 0 && text.indexOf(q) === -1);
      });
      document.querySelectorAll(".nav-section").forEach(function (sec) {
        var anyVisible = sec.querySelectorAll(".nav-page:not(.hidden)").length > 0;
        sec.style.display = (q.length > 0 && !anyVisible) ? "none" : "";
      });
    });
  }

  // ステータスバーの時刻(OSと同じ HH:MM)
  var timeEl = document.getElementById("sbTime");
  if (timeEl) {
    var tick = function () {
      var d = new Date();
      timeEl.textContent = String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0");
    };
    tick();
    setInterval(tick, 10000);
  }

  // テーマの切り替え(トップのページ・Playgroundと同じ鍵に覚える。読み込み時の適用は head.html)
  var themeBtn = document.getElementById("themeBtn");
  if (themeBtn) {
    themeBtn.addEventListener("click", function () {
      var root = document.documentElement;
      var cur = root.dataset.theme ||
        (matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
      var next = cur === "dark" ? "light" : "dark";
      root.dataset.theme = next;
      try { localStorage.setItem("picoos-theme", next); } catch (e) {}
    });
  }
})();
