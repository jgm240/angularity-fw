// Pulls the release list straight from GitHub in the visitor's browser (no server, no tracking).
(function () {
  var REPO = 'jgm240/angularity-fw';
  var API = 'https://api.github.com/repos/' + REPO + '/releases?per_page=12';
  var FEED = 'https://raw.githubusercontent.com/' + REPO + '/main/ota/directory.json';
  var RELEASES_PAGE = 'https://github.com/' + REPO + '/releases';

  function el(tag, attrs, children) {
    var e = document.createElement(tag);
    Object.keys(attrs || {}).forEach(function (k) {
      if (k === 'class') e.className = attrs[k];
      else if (k === 'text') e.textContent = attrs[k];
      else e.setAttribute(k, attrs[k]);
    });
    (children || []).forEach(function (c) { e.appendChild(c); });
    return e;
  }
  function mb(bytes) { return (bytes / 1048576).toFixed(1) + ' MB'; }
  function day(iso) {
    try { return new Date(iso).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' }); }
    catch (e) { return iso; }
  }
  function cleanNotes(body) {
    // short plain-text summary: first paragraph, no markdown noise
    var first = (body || '').split(/\r?\n\r?\n/)[0] || '';
    return first.replace(/[*_`#>]/g, '').trim();
  }
  function fetchJson(url) {
    return fetch(url, { headers: { 'Accept': 'application/json' } }).then(function (r) {
      if (!r.ok) throw new Error(url + ' ' + r.status);
      return r.json();
    });
  }
  function hashes() {
    return fetchJson(FEED).then(function (doc) {
      var map = {};
      ((doc.channels || [])[0] || { versions: [] }).versions.forEach(function (v) {
        var f = (v.files || [])[0];
        if (f && f.sha256) map[v.version] = f.sha256;
      });
      return map;
    }).catch(function () { return {}; });
  }

  // Release list on the downloads page
  var list = document.getElementById('release-list');
  if (list) {
    Promise.all([fetchJson(API), hashes()]).then(function (res) {
      var releases = res[0].filter(function (r) { return !r.draft; });
      var sums = res[1];
      list.textContent = '';
      if (!releases.length) { list.appendChild(el('p', { class: 'err', text: 'No releases found.' })); return; }
      releases.forEach(function (r, i) {
        var tgz = (r.assets || []).filter(function (a) { return /\.tgz$/.test(a.name); })[0];
        var title = el('h3', {}, [el('span', { text: r.name || r.tag_name })]);
        if (i === 0) title.appendChild(el('span', { class: 'pill', text: 'latest' }));
        var card = el('div', { class: 'release' }, [title,
          el('div', { class: 'meta', text: 'Published ' + day(r.published_at) + (tgz ? ' · ' + mb(tgz.size) : '') })]);
        var notes = cleanNotes(r.body);
        if (notes) card.appendChild(el('div', { class: 'notes', text: notes }));
        var btns = el('div', { class: 'btns' });
        if (tgz) btns.appendChild(el('a', { class: 'btn' + (i === 0 ? ' primary' : ''), href: tgz.browser_download_url, text: 'Download ' + tgz.name }));
        btns.appendChild(el('a', { class: 'btn', href: r.html_url, text: 'Release page' }));
        card.appendChild(btns);
        var sum = sums[r.tag_name];
        if (sum) card.appendChild(el('div', { class: 'hash', text: 'SHA-256 ' + sum }));
        list.appendChild(card);
      });
    }).catch(function () {
      list.textContent = '';
      list.appendChild(el('p', { class: 'err' }, [
        document.createTextNode('Could not load the release list (GitHub may be rate-limiting this browser). '),
        el('a', { href: RELEASES_PAGE, text: 'Open the releases page' }),
        document.createTextNode(' instead.')
      ]));
    });
  }

  // Small "latest release" badge on the home page
  var badge = document.getElementById('latest-release');
  if (badge) {
    fetchJson('https://api.github.com/repos/' + REPO + '/releases/latest').then(function (r) {
      badge.textContent = 'Latest: ' + (r.name || r.tag_name);
      badge.setAttribute('href', r.html_url);
    }).catch(function () { /* leave the static text */ });
  }
})();
