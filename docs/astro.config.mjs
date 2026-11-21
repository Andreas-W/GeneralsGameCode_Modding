// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import starlightLinksValidator from 'starlight-links-validator';

// Default GitHub Pages URL of the repo. With a custom domain: set `site` to it and drop `base`.
export default defineConfig({
	site: 'https://andreas-w.github.io',
	base: '/GeneralsGameCode_Modding',
	integrations: [
		starlight({
			title: 'GeneralsGameCode Modding',
			description: 'Modding documentation for the GeneralsGameCode_Modding fork of Command & Conquer: Generals Zero Hour.',
			social: [
				{ icon: 'github', label: 'GitHub', href: 'https://github.com/Andreas-W/GeneralsGameCode_Modding' },
			],
			editLink: {
				baseUrl: 'https://github.com/Andreas-W/GeneralsGameCode_Modding/edit/main/docs/',
			},
			lastUpdated: true,
			routeMiddleware: './src/routeData.ts',
			plugins: [starlightLinksValidator()],
			sidebar: [
				{ label: 'Overview', link: '/' },
				{ label: 'Changes and bug fixes', link: '/changes/' },
				{ label: 'Objects', items: [{ autogenerate: { directory: 'objects' } }] },
				{ label: 'Modules', items: [{ autogenerate: { directory: 'modules' } }] },
				{ label: 'Systems', items: [{ autogenerate: { directory: 'systems' } }] },
				{ label: 'INI files', items: [{ autogenerate: { directory: 'ini' } }] },
				{ label: 'Rendering', items: [{ autogenerate: { directory: 'rendering' } }] },
				{ label: 'Reference', items: [{ autogenerate: { directory: 'reference' } }] },
			],
		}),
	],
});
