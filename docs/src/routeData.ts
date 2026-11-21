import { defineRouteMiddleware } from '@astrojs/starlight/route-data';

const BANNERS: Record<string, string> = {
	unverified: 'This page was migrated from the old wiki and has not been checked against the code yet.',
	outdated: 'This page is known to be out of date with the code.',
};

export const onRequest = defineRouteMiddleware((context) => {
	const route = context.locals.starlightRoute;
	const data = route.entry.data as { status?: string; banner?: { content: string } };
	const text = data.status ? BANNERS[data.status] : undefined;
	if (text && !data.banner) {
		data.banner = { content: text };
	}
});
