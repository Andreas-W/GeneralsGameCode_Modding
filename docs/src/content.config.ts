import { defineCollection } from 'astro:content';
import { z } from 'astro/zod';
import { docsLoader } from '@astrojs/starlight/loaders';
import { docsSchema } from '@astrojs/starlight/schema';

export const collections = {
	docs: defineCollection({
		loader: docsLoader(),
		schema: docsSchema({
			extend: z
				.object({
					// Whether the page was checked against the code. Pages that are not
					// verified show a banner (see src/routeData.ts).
					status: z.enum(['verified', 'unverified', 'outdated', 'generated']).default('unverified'),
					// Commit the page was last checked against.
					verifiedAt: z.string().optional(),
					// Source files the page documents, relative to the repo root.
					sources: z.array(z.string()).default([]),
					// INI block or module name, links the page to its generated reference.
					ini: z.string().optional(),
				})
				.refine((d) => d.status !== 'verified' || !!d.verifiedAt, {
					message: 'verifiedAt is required when status is verified',
				}),
		}),
	}),
};
