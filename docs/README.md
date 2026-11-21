# Modding documentation site

Source of the documentation site, built with [Starlight](https://starlight.astro.build) and deployed to
GitHub Pages by `.github/workflows/docs.yml` on every push to `main`.

- Pages: `src/content/docs/` (Markdown). The folder layout is the sidebar layout.
- Frontmatter: besides Starlight's fields, every page has `status` (`verified`, `unverified`, `outdated`,
  `generated`), `verifiedAt` (commit the page was checked against, required when verified), `sources`
  (source files it documents) and `ini` (module or INI block name). See `src/content.config.ts`.
- Links between pages are checked at build time: a broken link or anchor fails the build.

Run locally (Node 22 or newer):

```
cd docs
npm install
npm run dev      # http://localhost:4321/GeneralsGameCode_Modding/
npm run build    # what CI runs
```

The other Markdown files directly in `docs/` (DirectX 12 investigation notes) are developer notes and
are not part of the site.
