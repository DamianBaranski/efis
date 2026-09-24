
(function () {
  var CX = 300;
  var CY = 300;
  var OWN_X = 300;
  var OWN_Y = 372;
  var VDI_TRAVEL = 50;
  var VDI_PEG = 56;
  var MAP_W = 1000;
  var MAP_H = 520;
  var TIME_SCALE = 30;

  var NS = "http://www.w3.org/2000/svg";
  var rose = document.getElementById("rose");
  var dialRoute = document.getElementById("dial-route");
  var dialPoints = document.getElementById("dial-points");
  var vdiSymbol = document.getElementById("vdi-symbol");
  var vdiDiamond = document.getElementById("vdi-diamond");
  var vdiCaret = document.getElementById("vdi-caret");
  var playBtn = document.getElementById("btn-play");
  var modeEl = document.getElementById("sim-mode");

  function el(name, attrs) {
    var node = document.createElementNS(NS, name);
    for (var key in attrs) node.setAttribute(key, attrs[key]);
    return node;
  }

  function polar(deg, r) {
    var a = deg * Math.PI / 180;
    return [CX + r * Math.sin(a), CY - r * Math.cos(a)];
  }

  function buildRose() {
    rose.appendChild(el("circle", {
      cx: "300", cy: "300", r: "188",
      fill: "none", stroke: "#2a2a2a", "stroke-width": "1"
    }));
    var deg;
    for (deg = 0; deg < 360; deg += 5) {
      var card = deg % 30 === 0;
      var major = deg % 10 === 0;
      var innerR = card ? 164 : major ? 172 : 180;
      var outer = polar(deg, 188);
      var inner = polar(deg, innerR);
      rose.appendChild(el("line", {
        x1: inner[0].toFixed(2),
        y1: inner[1].toFixed(2),
        x2: outer[0].toFixed(2),
        y2: outer[1].toFixed(2),
        stroke: card ? "#f4f4f4" : major ? "#d0d0d0" : "#7d7d7d",
        "stroke-width": card ? "2.2" : major ? "1.7" : "1.15",
        "stroke-linecap": "butt"
      }));
    }
    var labels = [
      [0, "N", true], [30, "3", false], [60, "6", false], [90, "E", true],
      [120, "12", false], [150, "15", false], [180, "S", true], [210, "21", false],
      [240, "24", false], [270, "W", true], [300, "30", false], [330, "33", false]
    ];
    for (var i = 0; i < labels.length; i++) {
      var item = labels[i];
      var p = polar(item[0], 152);
      var text = el("text", {
        x: p[0].toFixed(2),
        y: p[1].toFixed(2),
        "text-anchor": "middle",
        "dominant-baseline": "middle",
        transform: "rotate(" + item[0] + " " + p[0].toFixed(2) + " " + p[1].toFixed(2) + ")"
      });
      text.setAttribute("class", item[2] ? "rose-card" : "rose-num");
      text.textContent = item[1];
      rose.appendChild(text);
    }
  }

  function clamp(v, lo, hi) {
    return Math.min(hi, Math.max(lo, v));
  }

  function norm360(d) {
    var x = d % 360;
    if (x < 0) x += 360;
    return x;
  }

  function smooth(t) {
    t = clamp(t, 0, 1);
    return t * t * (3 - 2 * t);
  }

  function lerp(a, b, t) {
    return a + (b - a) * t;
  }

  function formatBrg(deg) {
    var d = Math.round(norm360(deg)) % 360;
    var s = String(d);
    while (s.length < 3) s = "0" + s;
    return s;
  }

  function formatDist(nm) {
    return (Math.round(nm * 10) / 10).toFixed(1);
  }

  function formatVdev(dots) {
    var v = Math.round(dots * 100) / 100;
    return v.toFixed(2) + " dot";
  }

  function show(node, on) {
    node.setAttribute("display", on ? "" : "none");
  }

  function defaultRoute() {
    return [
      { name: "KSXU", e: 0, n: 0 },
      { name: "KAEG", e: 0.628, n: 17.989 },
      { name: "KABQ", e: 11.339, n: 29.879 },
      { name: "KROW", e: 14, n: 4 }
    ];
  }

  function defaultAircraft() {
    return { e: 0.849, n: 9.977 };
  }

  var route = defaultRoute();
  var ac = defaultAircraft();
  var sim = { track: 2, gs: 155, scale: 20, vdev: -0.55, leg: 0 };
  var selected = 1;
  var data = null;
  var running = false;
  var lastTs = null;
  var syncing = false;
  var drag = null;
  var bounds = null;
  var mapSvg = document.getElementById("map");
  var mapGrid = document.getElementById("map-grid");
  var mapRoute = document.getElementById("map-route");
  var mapPoints = document.getElementById("map-points");
  var mapAc = document.getElementById("map-ac");
  var nameInput = document.getElementById("pt-name");

  function legCourseOf(a, b) {
    return norm360(Math.atan2(b.e - a.e, b.n - a.n) * 180 / Math.PI);
  }

  function project(pos, a, b) {
    var de = b.e - a.e;
    var dn = b.n - a.n;
    var len = Math.sqrt(de * de + dn * dn);
    if (len < 1e-4) return { along: 0, xte: 0, len: 0.0001 };
    var ue = de / len;
    var un = dn / len;
    var ve = pos.e - a.e;
    var vn = pos.n - a.n;
    return {
      along: ve * ue + vn * un,
      xte: ve * un + vn * (-ue),
      len: len
    };
  }

  function nearestLeg() {
    var best = 0;
    var bestD = 1e9;
    var i;
    for (i = 0; i < route.length; i++) {
      var a = route[i];
      var b = route[(i + 1) % route.length];
      var proj = project(ac, a, b);
      var d = Math.abs(proj.xte);
      if (proj.along < -0.5) d += -proj.along;
      if (proj.along > proj.len + 0.5) d += proj.along - proj.len;
      if (d < bestD) {
        bestD = d;
        best = i;
      }
    }
    sim.leg = best;
  }

  function sequence() {
    var n;
    for (n = 0; n < route.length; n++) {
      var a = route[sim.leg];
      var b = route[(sim.leg + 1) % route.length];
      var proj = project(ac, a, b);
      if (proj.along > proj.len + 0.08) sim.leg = (sim.leg + 1) % route.length;
      else break;
    }
  }

  function navSolution() {
    var i = sim.leg % route.length;
    var a = route[i];
    var b = route[(i + 1) % route.length];
    var c = route[(i + 2) % route.length];
    var proj = project(ac, a, b);
    var de = b.e - ac.e;
    var dn = b.n - ac.n;
    return {
      track: sim.track,
      legCourse: legCourseOf(a, b),
      nextCourse: legCourseOf(b, c),
      distToFix: proj.len - proj.along,
      along: proj.along,
      legLen: proj.len,
      nextLen: Math.sqrt((c.e - b.e) * (c.e - b.e) + (c.n - b.n) * (c.n - b.n)),
      xte: proj.xte,
      scale: sim.scale,
      dist: Math.sqrt(de * de + dn * dn),
      brg: norm360(Math.atan2(de, dn) * 180 / Math.PI),
      nextBrg: norm360(Math.atan2(c.e - ac.e, c.n - ac.n) * 180 / Math.PI),
      gs: sim.gs,
      vdev: sim.vdev,
      wpt: b.name || "WPT",
      fix: b.name || "WPT"
    };
  }

  function worldScreen(e, n) {
    var px = 200 / Math.max(0.5, data.scale);
    var de = e - ac.e;
    var dn = n - ac.n;
    var tr = data.track * Math.PI / 180;
    var fwd = de * Math.sin(tr) + dn * Math.cos(tr);
    var right = de * Math.cos(tr) - dn * Math.sin(tr);
    return [OWN_X + right * px, OWN_Y - fwd * px];
  }

  function paintDialRoute() {
    var d = "";
    var i;
    for (i = 0; i < route.length; i++) {
      var p = worldScreen(route[i].e, route[i].n);
      d += (i ? "L" : "M") + p[0].toFixed(1) + " " + p[1].toFixed(1) + " ";
    }
    if (route.length > 2) d += "Z";
    dialRoute.setAttribute("d", d);
    while (dialPoints.firstChild) dialPoints.removeChild(dialPoints.firstChild);
    for (i = 0; i < route.length; i++) {
      var at = worldScreen(route[i].e, route[i].n);
      var g = el("g", {
        transform: "translate(" + at[0].toFixed(1) + " " + at[1].toFixed(1) + ")"
      });
      g.appendChild(el("circle", {
        r: "6",
        fill: "#141414",
        stroke: "#ff2fd0",
        "stroke-width": "1.8"
      }));
      var label = el("text", {
        x: "10", y: "-8",
        fill: "#f2f2f2",
        "font-size": "13",
        "text-anchor": "start"
      });
      label.setAttribute("class", "fix-id");
      label.textContent = route[i].name || "WPT";
      g.appendChild(label);
      dialPoints.appendChild(g);
    }
  }

  function paintVdi() {
    var pegged = Math.abs(data.vdev) >= 1;
    if (pegged) {
      var y = 298 + (data.vdev < 0 ? VDI_PEG : -VDI_PEG);
      vdiSymbol.setAttribute("transform", "translate(382 " + y + ")");
      vdiCaret.setAttribute("transform", data.vdev > 0 ? "scale(1 -1)" : "");
      show(vdiDiamond, false);
      show(vdiCaret, true);
    } else {
      var yDot = 298 + (-data.vdev) * VDI_TRAVEL;
      vdiSymbol.setAttribute("transform", "translate(382 " + yDot.toFixed(1) + ")");
      vdiCaret.setAttribute("transform", "");
      show(vdiDiamond, true);
      show(vdiCaret, false);
    }
  }

  function paint(next) {
    data = next;
    rose.setAttribute("transform", "rotate(" + (-data.track) + " " + CX + " " + CY + ")");
    paintDialRoute();
    paintVdi();
  }

  function setNum(id, value) {
    var node = document.getElementById(id);
    node.value = String(clamp(value, parseFloat(node.min), parseFloat(node.max)));
  }

  function paintReadouts() {
    document.getElementById("out-track").textContent = formatBrg(sim.track) + "°";
    document.getElementById("out-gs").textContent = Math.round(sim.gs) + " kt";
    document.getElementById("out-scale").textContent = Math.round(sim.scale) + " nm";
    document.getElementById("out-vdev").textContent = formatVdev(sim.vdev);
    document.getElementById("out-leg").textContent = formatBrg(data.legCourse) + "°";
    document.getElementById("out-next").textContent = formatBrg(data.nextCourse) + "°";
    document.getElementById("out-dist").textContent = formatDist(data.dist) + " nm";
    document.getElementById("out-brg").textContent = formatBrg(data.brg) + "°";
    document.getElementById("out-xte").textContent = (Math.round(data.xte * 100) / 100).toFixed(2) + " nm";
  }

  function contentBounds() {
    var minE = ac.e;
    var maxE = ac.e;
    var minN = ac.n;
    var maxN = ac.n;
    var i;
    for (i = 0; i < route.length; i++) {
      minE = Math.min(minE, route[i].e);
      maxE = Math.max(maxE, route[i].e);
      minN = Math.min(minN, route[i].n);
      maxN = Math.max(maxN, route[i].n);
    }
    var padE = Math.max(2, (maxE - minE) * 0.18);
    var padN = Math.max(2, (maxN - minN) * 0.18);
    return { minE: minE - padE, maxE: maxE + padE, minN: minN - padN, maxN: maxN + padN };
  }

  function fitBounds() {
    bounds = contentBounds();
  }

  function growBounds() {
    var b = contentBounds();
    if (!bounds) {
      bounds = b;
      return;
    }
    bounds.minE = Math.min(bounds.minE, b.minE);
    bounds.maxE = Math.max(bounds.maxE, b.maxE);
    bounds.minN = Math.min(bounds.minN, b.minN);
    bounds.maxN = Math.max(bounds.maxN, b.maxN);
  }

  function mapScale() {
    var spanE = Math.max(0.5, bounds.maxE - bounds.minE);
    var spanN = Math.max(0.5, bounds.maxN - bounds.minN);
    var s = Math.min((MAP_W - 48) / spanE, (MAP_H - 48) / spanN);
    return {
      s: s,
      ox: (MAP_W - spanE * s) / 2,
      oy: (MAP_H - spanN * s) / 2
    };
  }

  function worldToSvg(e, n) {
    var m = mapScale();
    return [
      m.ox + (e - bounds.minE) * m.s,
      m.oy + (bounds.maxN - n) * m.s
    ];
  }

  function svgToWorld(x, y) {
    var m = mapScale();
    return {
      e: bounds.minE + (x - m.ox) / m.s,
      n: bounds.maxN - (y - m.oy) / m.s
    };
  }

  function eventWorld(evt) {
    var pt = mapSvg.createSVGPoint();
    pt.x = evt.clientX;
    pt.y = evt.clientY;
    var p = pt.matrixTransform(mapSvg.getScreenCTM().inverse());
    return svgToWorld(p.x, p.y);
  }

  function paintMap() {
    if (!bounds) fitBounds();
    var m = mapScale();
    while (mapGrid.firstChild) mapGrid.removeChild(mapGrid.firstChild);
    var step = 5;
    var e0 = Math.floor(bounds.minE / step) * step;
    var n0 = Math.floor(bounds.minN / step) * step;
    var e, n, p1, p2, i;
    for (e = e0; e <= bounds.maxE + step; e += step) {
      p1 = worldToSvg(e, bounds.minN);
      p2 = worldToSvg(e, bounds.maxN);
      mapGrid.appendChild(el("line", {
        x1: p1[0], y1: p1[1], x2: p2[0], y2: p2[1],
        stroke: "#1c2a22", "stroke-width": "1"
      }));
    }
    for (n = n0; n <= bounds.maxN + step; n += step) {
      p1 = worldToSvg(bounds.minE, n);
      p2 = worldToSvg(bounds.maxE, n);
      mapGrid.appendChild(el("line", {
        x1: p1[0], y1: p1[1], x2: p2[0], y2: p2[1],
        stroke: "#1c2a22", "stroke-width": "1"
      }));
    }
    var north = worldToSvg(bounds.minE + 1.2, bounds.maxN - 1.2);
    var northTip = worldToSvg(bounds.minE + 1.2, bounds.maxN - 0.2);
    mapGrid.appendChild(el("line", {
      x1: north[0], y1: north[1], x2: northTip[0], y2: northTip[1],
      stroke: "#9ad0b0", "stroke-width": "2"
    }));
    var nLabel = el("text", {
      x: northTip[0], y: northTip[1] - 6,
      fill: "#9ad0b0", "font-size": "14", "text-anchor": "middle"
    });
    nLabel.textContent = "N";
    mapGrid.appendChild(nLabel);

    function svgPath(pts, close) {
      if (pts.length < 2) return "";
      var path = "";
      var j;
      for (j = 0; j < pts.length; j++) {
        var sp = worldToSvg(pts[j].e, pts[j].n);
        path += (j ? "L" : "M") + sp[0].toFixed(1) + " " + sp[1].toFixed(1) + " ";
      }
      if (close) path += "Z";
      return path;
    }
    mapRoute.setAttribute("d", svgPath(route, true));

    while (mapPoints.firstChild) mapPoints.removeChild(mapPoints.firstChild);
    for (i = 0; i < route.length; i++) {
      var at = worldToSvg(route[i].e, route[i].n);
      var g = el("g", { class: "pt", transform: "translate(" + at[0].toFixed(1) + " " + at[1].toFixed(1) + ")" });
      g.setAttribute("data-i", String(i));
      var isSel = i === selected;
      g.appendChild(el("circle", {
        r: "14", fill: "transparent"
      }));
      g.appendChild(el("circle", {
        r: isSel ? "8" : "6",
        fill: "#141414",
        stroke: isSel ? "#ffffff" : "#ff2fd0",
        "stroke-width": isSel ? "2.4" : "1.6"
      }));
      var label = el("text", {
        x: "12", y: "-10",
        fill: "#f2f2f2",
        "font-size": "15"
      });
      label.textContent = route[i].name || "WPT";
      g.appendChild(label);
      mapPoints.appendChild(g);
    }

    var ap = worldToSvg(ac.e, ac.n);
    mapAc.setAttribute("transform", "translate(" + ap[0].toFixed(1) + " " + ap[1].toFixed(1) + ") rotate(" + sim.track + ")");
    while (mapAc.firstChild) mapAc.removeChild(mapAc.firstChild);
    mapAc.appendChild(el("path", {
      d: "M0,-14 L8,10 L0,6 L-8,10 Z",
      fill: "#f4f4f4",
      stroke: "#39ff6a",
      "stroke-width": "1.2"
    }));
    mapAc.appendChild(el("line", {
      x1: "0", y1: "0", x2: "0", y2: "-36",
      stroke: "#39ff6a", "stroke-width": "1.5"
    }));
  }

  function refreshNameField() {
    var pt = route[selected];
    nameInput.value = pt ? pt.name : "";
    nameInput.disabled = !pt;
  }

  function frame() {
    if (running && sim.gs > 0 && !drag) sequence();
    paint(navSolution());
    paintReadouts();
    paintMap();
  }

  function start() {
    if (running) return;
    running = true;
    lastTs = null;
    playBtn.textContent = "Pause";
    modeEl.textContent = "SIM";
    modeEl.classList.remove("manual");
  }

  function stop() {
    running = false;
    lastTs = null;
    playBtn.textContent = "Play";
    modeEl.textContent = "MANUAL";
    modeEl.classList.add("manual");
  }

  function readSliders() {
    sim.track = parseFloat(document.getElementById("sl-track").value);
    sim.gs = parseFloat(document.getElementById("sl-gs").value);
    sim.scale = parseFloat(document.getElementById("sl-scale").value);
    sim.vdev = parseFloat(document.getElementById("sl-vdev").value);
  }

  function syncSliders() {
    syncing = true;
    setNum("sl-track", norm360(sim.track));
    setNum("sl-gs", sim.gs);
    setNum("sl-scale", sim.scale);
    setNum("sl-vdev", sim.vdev);
    syncing = false;
  }

  function tick(now) {
    if (running && sim.gs > 0 && !drag) {
      if (lastTs != null) {
        var dt = Math.min(0.05, (now - lastTs) / 1000);
        var dist = (sim.gs / 3600) * TIME_SCALE * dt;
        var rad = sim.track * Math.PI / 180;
        ac.e += Math.sin(rad) * dist;
        ac.n += Math.cos(rad) * dist;
        growBounds();
      }
      lastTs = now;
    } else {
      lastTs = null;
    }
    frame();
    requestAnimationFrame(tick);
  }

  function insertOnSegment(e, n) {
    var best = 0;
    var bestD = 1e9;
    var bestT = 0.5;
    var i;
    for (i = 0; i < route.length; i++) {
      var a = route[i];
      var b = route[(i + 1) % route.length];
      var proj = project({ e: e, n: n }, a, b);
      var t = clamp(proj.along / proj.len, 0, 1);
      var pe = a.e + (b.e - a.e) * t;
      var pn = a.n + (b.n - a.n) * t;
      var d = Math.sqrt((e - pe) * (e - pe) + (n - pn) * (n - pn));
      if (d < bestD) {
        bestD = d;
        best = i;
        bestT = t;
      }
    }
    var a0 = route[best];
    var b0 = route[(best + 1) % route.length];
    var pt = {
      name: "WP" + (route.length + 1),
      e: a0.e + (b0.e - a0.e) * bestT,
      n: a0.n + (b0.n - a0.n) * bestT
    };
    if (bestD > 1.5) {
      pt.e = e;
      pt.n = n;
    }
    route.splice(best + 1, 0, pt);
    if (sim.leg > best) sim.leg++;
    selected = best + 1;
    refreshNameField();
    growBounds();
  }

  playBtn.addEventListener("click", function () {
    if (running) stop();
    else start();
  });

  document.getElementById("btn-reset").addEventListener("click", function () {
    route = defaultRoute();
    ac = defaultAircraft();
    sim.track = 2;
    sim.gs = 155;
    sim.scale = 20;
    sim.vdev = -0.55;
    sim.leg = 0;
    selected = 1;
    bounds = null;
    refreshNameField();
    syncSliders();
    frame();
  });

  document.getElementById("btn-add").addEventListener("click", function () {
    var i = selected;
    if (i < 0) i = sim.leg;
    var a = route[i];
    var b = route[(i + 1) % route.length];
    route.splice(i + 1, 0, {
      name: "WP" + (route.length + 1),
      e: (a.e + b.e) / 2,
      n: (a.n + b.n) / 2
    });
    if (sim.leg > i) sim.leg++;
    selected = i + 1;
    refreshNameField();
    growBounds();
    frame();
  });

  document.getElementById("btn-remove").addEventListener("click", function () {
    if (route.length <= 3 || selected < 0) return;
    route.splice(selected, 1);
    if (sim.leg >= route.length) sim.leg = 0;
    if (sim.leg > selected) sim.leg--;
    else if (sim.leg === selected) sim.leg = sim.leg % route.length;
    selected = Math.min(selected, route.length - 1);
    refreshNameField();
    frame();
  });

  document.getElementById("btn-track-leg").addEventListener("click", function () {
    sim.track = data.legCourse;
    syncSliders();
    frame();
  });

  document.getElementById("btn-fit").addEventListener("click", function () {
    fitBounds();
    frame();
  });

  nameInput.addEventListener("input", function () {
    if (selected < 0) return;
    var name = nameInput.value.toUpperCase().replace(/[^A-Z0-9]/g, "").slice(0, 6);
    if (name !== nameInput.value) nameInput.value = name;
    route[selected].name = name || "WPT";
    frame();
  });

  var inputs = document.querySelectorAll(".panel input");
  for (var s = 0; s < inputs.length; s++) {
    inputs[s].addEventListener("input", function () {
      if (syncing) return;
      readSliders();
      frame();
    });
  }

  mapSvg.addEventListener("pointerdown", function (evt) {
    var t = evt.target;
    while (t && t !== mapSvg && !t.getAttribute("data-i") && t.id !== "map-ac" && t.parentNode && t.parentNode.id !== "map-ac") {
      t = t.parentNode;
    }
    if (t && t.getAttribute && t.getAttribute("data-i") != null && t !== mapSvg) {
      selected = parseInt(t.getAttribute("data-i"), 10);
      drag = { kind: "pt", i: selected };
      refreshNameField();
      mapSvg.setPointerCapture(evt.pointerId);
      evt.preventDefault();
    } else if (t === mapAc || (t.parentNode && t.parentNode.id === "map-ac")) {
      drag = { kind: "ac" };
      mapSvg.setPointerCapture(evt.pointerId);
      evt.preventDefault();
    }
  });

  mapSvg.addEventListener("pointermove", function (evt) {
    if (!drag) return;
    var w = eventWorld(evt);
    if (drag.kind === "pt") {
      route[drag.i].e = w.e;
      route[drag.i].n = w.n;
      growBounds();
    } else {
      ac.e = w.e;
      ac.n = w.n;
      nearestLeg();
      growBounds();
    }
    frame();
  });

  mapSvg.addEventListener("pointerup", function () {
    drag = null;
  });

  mapSvg.addEventListener("dblclick", function (evt) {
    if (evt.target.closest && evt.target.closest(".pt, .ac")) return;
    var w = eventWorld(evt);
    insertOnSegment(w.e, w.n);
    frame();
  });

  buildRose();
  nearestLeg();
  fitBounds();
  refreshNameField();
  syncSliders();

  var params = new URLSearchParams(location.search);
  var reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  if (!params.has("pause") && !reduce) start();
  else stop();
  frame();
  requestAnimationFrame(tick);
})();
