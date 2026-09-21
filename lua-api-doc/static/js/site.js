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
})();
