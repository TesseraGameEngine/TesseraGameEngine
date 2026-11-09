// Tessera Engine Docs Interactive Navigation & Search
document.addEventListener('DOMContentLoaded', () => {
  // Engine version (docs/version.js, generated from the repo's VERSION file)
  const version = window.TESSERA_VERSION;
  if (version) {
    const title = document.querySelector('.sidebar-header h2');
    if (title) {
      const badge = document.createElement('span');
      badge.className = 'version-badge';
      badge.textContent = 'v' + version;
      badge.title = 'Tessera Engine version ' + version;
      title.appendChild(badge);
    }
    document.querySelectorAll('.tessera-version').forEach(el => { el.textContent = version; });
  }

  const searchInput = document.getElementById('doc-search');
  const navItems = document.querySelectorAll('.nav-item');
  const navGroups = document.querySelectorAll('.nav-group');

  if (searchInput) {
    searchInput.addEventListener('input', (e) => {
      const q = e.target.value.toLowerCase().trim();
      navGroups.forEach(group => {
        let hasVisibleChild = false;
        const items = group.querySelectorAll('.nav-item');
        items.forEach(item => {
          const text = item.textContent.toLowerCase();
          if (text.includes(q)) {
            item.style.display = 'block';
            hasVisibleChild = true;
          } else {
            item.style.display = 'none';
          }
        });
        group.style.display = hasVisibleChild ? 'block' : 'none';
      });
    });
  }

  // Keyboard navigation for Prev/Next
  document.addEventListener('keydown', (e) => {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
    if (e.key === 'ArrowLeft') {
      const prevCard = document.querySelector('.pagination-card.prev');
      if (prevCard && prevCard.href) window.location.href = prevCard.href;
    } else if (e.key === 'ArrowRight') {
      const nextCard = document.querySelector('.pagination-card.next');
      if (nextCard && nextCard.href) window.location.href = nextCard.href;
    }
  });
});
