(() => {
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const reveals = document.querySelectorAll('.reveal');

  if (reducedMotion || !('IntersectionObserver' in window)) {
    reveals.forEach((element) => element.classList.add('is-visible'));
  } else {
    const observer = new IntersectionObserver((entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add('is-visible');
          observer.unobserve(entry.target);
        }
      });
    }, { threshold: 0.12 });
    reveals.forEach((element) => observer.observe(element));
  }

  document.querySelectorAll('[data-replay]').forEach((button) => {
    button.addEventListener('click', () => {
      const demo = button.closest('[data-flow-demo]');
      demo.querySelectorAll('.packet-moving, .map-arrow, .payload-cells').forEach((element) => {
        element.style.animation = 'none';
        void element.offsetWidth;
        element.style.animation = '';
      });
    });
  });

  document.querySelectorAll('[data-tabs]').forEach((tabs) => {
    const buttons = [...tabs.querySelectorAll('[role="tab"]')];
    const activate = (button) => {
      buttons.forEach((candidate) => {
        const selected = candidate === button;
        candidate.setAttribute('aria-selected', selected ? 'true' : 'false');
        candidate.tabIndex = selected ? 0 : -1;
        document.getElementById(candidate.getAttribute('aria-controls')).hidden = !selected;
      });
    };

    buttons.forEach((button, index) => {
      button.addEventListener('click', () => activate(button));
      button.addEventListener('keydown', (event) => {
        if (!['ArrowLeft', 'ArrowRight'].includes(event.key)) return;
        event.preventDefault();
        const direction = event.key === 'ArrowRight' ? 1 : -1;
        const next = buttons[(index + direction + buttons.length) % buttons.length];
        activate(next);
        next.focus();
      });
    });
  });

  document.querySelectorAll('[data-copy-target]').forEach((button) => {
    button.addEventListener('click', async () => {
      const target = document.getElementById(button.dataset.copyTarget);
      if (!target || !navigator.clipboard) return;
      await navigator.clipboard.writeText(target.innerText);
      const original = button.textContent;
      button.textContent = 'Copied';
      window.setTimeout(() => { button.textContent = original; }, 1400);
    });
  });
})();
